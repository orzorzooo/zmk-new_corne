# 觸控板整合說明（trackpad 分支）

> 最後更新：2026-07-17
> 硬體：Azoteq IQS5xx 系列觸控板（TPS43），安裝於**右半**，逆時針旋轉 90 度安裝

## 目前狀態

| 項目 | 狀態 |
|---|---|
| 分體角色 | 左半 = central（主控，跑 ZMK Studio）；右半 = peripheral |
| 觸控板位置 | 右半，接 I2C1（SDA=P1.08、SCL=P0.24、RST=P1.04、RDY=P1.07），位址 `0x74` |
| 事件轉送 | 右半透過 `zmk,input-split` 將指標事件轉送到左半 central |
| 方向設定 | Central 端共用 `TPS43_ROTATION_CORRECTION = XY_SWAP \| Y_INVERT`，同時修正游標與雙指捲動軸向 |
| 手勢 | 單指點擊=左鍵、雙指點擊=右鍵、press-and-hold（250ms）、雙軸捲動；tap 的 press/release 由本地 driver 同步送出 |

## 架構總覽

觸控板事件的路徑：

```
TPS43 (右半 I2C1)
  → 專案內 `tps43_iqs5xx` 驅動（以 AYM1607 driver 為基礎）
  → tps43_split (zmk,input-split, 右半為發送端)
  → BLE split 傳輸
  → tps43_split (左半 central 為接收端)
  → tps43_input (zmk,input-listener)
  → HID 指標回報 → 電腦
```

相關檔案：

- [boards/arm/eyelash_corne/eyelash_corne.dtsi](../boards/arm/eyelash_corne/eyelash_corne.dtsi) — `tps43` 節點（I2C 裝置、手勢、方向）、`tps43_split`、`tps43_input`
- [boards/arm/eyelash_corne/eyelash_corne_right.dts](../boards/arm/eyelash_corne/eyelash_corne_right.dts) — 右半啟用 `&i2c1` 與 `&tps43`
- [boards/arm/eyelash_corne/eyelash_corne_left.dts](../boards/arm/eyelash_corne/eyelash_corne_left.dts) — 左半啟用 i2c1 但 `&tps43` 保持 disabled（實體不在左邊）
- [src/tps43_iqs5xx.c](../src/tps43_iqs5xx.c) — 專案維護的 TPS43 driver；tap 在同一次事件內送出按下與放開
- [config/eyelash_corne.conf](../config/eyelash_corne.conf) — 啟用 input/pointing，並停用 dependency 內原始 driver，避免重複註冊裝置
- [config/west.yml](../config/west.yml) — 固定 AYM1607 dependency commit（提供 DTS binding/Kconfig）與 cormoran 的 ZMK fork（`v0.3-branch+dya`）

## 點擊卡住修正（2026-07-17）

原始 driver 將一次 tap 拆成兩個獨立事件：先送 `BTN_0=1`，再由右半的 delayable work 於 100ms 後送 `BTN_0=0`。同時，這版 cormoran split central 使用有限的 `K_NO_WAIT` queue；事件壅塞時不會重送。若獨立的 release 沒有執行或在 split queue 被丟棄，主機就只收到 mouse button down，直到裝置斷線才會清除。

目前採兩層修正：

1. 專案內 driver 讓單指與雙指 tap 在同一次 TPS43 gesture handler 內依序送出 press/release，不再依賴 peripheral 的 100ms 延遲工作。
2. 左半 central 將 `CONFIG_ZMK_SPLIT_BLE_CENTRAL_POSITION_QUEUE_SIZE` 提高到 64，替 TPS43 的 X/Y burst 與按鍵事件保留足夠空間。

`press-and-hold` 仍是獨立的持續按下/放開狀態，因此拖曳功能保留，不會被 tap 修正提前放開。

## 方向設定的原理（背景知識）

> 本專案把 TPS43 的安裝方向修正統一放在 central 端 input processor。`tps43` 晶片端不使用 `switch-xy` / `flip-x` / `flip-y`，避免右半晶片暫存器和左半 listener 兩層同時改方向。

`switch-xy` / `flip-x` / `flip-y` **不是軟體轉換**：驅動在初始化時把這三個旗標寫進觸控板晶片的 `XY_CONFIG_0` 暫存器（位址 `0x0669`），之後晶片直接回報轉換後的座標。

依 Azoteq IQS5xx-B000 datasheet（§5.8 XY Output Flip & Switch、§8.10 暫存器定義），三個旗標的精確語意是：

- `SWITCH_XY_AXIS`（bit 2）：切換 X/Y **輸出**由哪組電極負責（Rx 欄 ↔ Tx 列）
- `FLIP_Y`（bit 1）：翻轉**最終輸出**的 Y 值
- `FLIP_X`（bit 0）：翻轉**最終輸出**的 X 值

**關鍵：flip 作用在交換「之後」的輸出軸上。** 如果未來要重新測右半晶片端方向，正確思考方式是兩步驟：

1. 游標動的軸不對（手指上下移動游標卻左右動）→ 切換 `switch-xy`
2. 軸對了但某個方向相反 → 游標**左右**相反就切換 `flip-x`、**上下**相反就切換 `flip-y`，兩者互不影響

三個旗標共 8 種組合，其中 4 種是純旋轉（0/90/180/270 度）、4 種是鏡像。若實測發現「只有一個軸相反」，代表需要的是含鏡像的組合，這在感應器安裝面向與預期相反時是正常的。

實務上的另一個關鍵點：如果改的是 `switch-xy` / `flip-*`，必須重刷「右半」韌體，因為暫存器初始化發生在觸控板所在的右半。現在本專案改的是 central input processor，所以方向修正主要刷左半生效。

### 目前實測與定案（2026-07-17）

目前韌體在 `zip_xy_transform Y_INVERT` + `zip_scroll_transform Y_INVERT` 下的實測結果：

- 手指往上，游標向右
- 手指往右，游標向上
- 雙指向右，頁面向下捲動

這代表游標與 scroll 都還差一個軸交換；而現有 Y 方向修正不能拿掉，否則會回到「上→右、右→下」的舊行為。定案做法：

- 晶片端（`tps43` 節點）：不設 `switch-xy` / `flip-x` / `flip-y`
- Driver 端：保留 `natural-scroll-x` / `natural-scroll-y`，讓捲動內容跟手指方向一致
- Central 端（`tps43_input` listener）：pointer 和 wheel 共用同一個旋轉修正

```dts
#define TPS43_ROTATION_CORRECTION (INPUT_TRANSFORM_XY_SWAP | INPUT_TRANSFORM_Y_INVERT)

input-processors = <&zip_xy_transform TPS43_ROTATION_CORRECTION>,
                   <&zip_scroll_transform TPS43_ROTATION_CORRECTION>;
```

`zip_xy_transform` 修游標；`zip_scroll_transform` 修 wheel / horizontal wheel。兩者共用同一個常數，避免之後只修到游標卻忘了捲動。預期結果是：手指往上 = 游標往上；手指往右 = 游標往右；雙指往上 = 頁面向下捲動（macOS 自然手勢）。

### 未來校正 SOP

方向不對時，一律調 `TPS43_ROTATION_CORRECTION`（**改完刷左半**）。不要先動 `tps43` 節點的 `switch-xy` / `flip-*`，除非要重新做一輪右半晶片暫存器測試。

1. 做兩個單軸測試：手指往上游標往哪、手指往右游標往哪。
2. 兩軸都對 → 完成。
3. 只有左右相反 → 切換 `INPUT_TRANSFORM_X_INVERT`；只有上下相反 → 切換 `INPUT_TRANSFORM_Y_INVERT`。
4. 差 90 度（上變左右、右變上下）→ 切換 `INPUT_TRANSFORM_XY_SWAP`，重測後用步驟 3 修掉殘餘反向。
5. 全部相反（180 度）→ 同時切換兩個 INVERT。
6. 捲動軸向應跟游標同一套修正；如果只有捲動方向反了，再單獨調 `natural-scroll-x/y` 或 `zip_scroll_transform` 的 invert flag。

## 異動紀錄（trackpad 分支）

| Commit | 內容 |
|---|---|
| （未 commit）2026-07-17 | **修正 tap 卡在 mouse button down**：專案內維護 TPS43 driver，tap 同步送出 press/release，並將 central split queue 提高到 64。此修改需左右兩半都刷 |
| `57b22d4` | **依最新實測重構 TPS43 方向處理**：新增 `TPS43_ROTATION_CORRECTION = XY_SWAP \| Y_INVERT`，pointer 與 scroll 共用同一個 central 端修正。解決「上→右、右→上、雙指右→頁面下」的軸交換問題。**此修改需刷左半** |
| `8bc8a9d` | 改為 `zip_xy_transform Y_INVERT` + `zip_scroll_transform Y_INVERT`；實測仍有 X/Y 軸交換，已被上列修改取代 |
| `a4a669d` 等五筆 | 2026-07-16 當天的方向迭代（晶片旗標 flip-y / switch-xy / flip-x / switch-xy → processor XY_SWAP\|Y_INVERT），最後改由 central processor 統一修正 |
| `ea4238d` | 設定順時針 90 度（保留 `switch-xy`、加 `flip-x`）— 已被上列變更取代 |
| `fcfa977` | 加入 `zmk,input-split` 節點，讓 peripheral 的指標事件轉送到 central |
| `2b597f4` | 全域啟用 input subsystem 與 Azoteq IQS5xx 驅動 |
| `57f7768` | 修正分體角色：左半為 central，ZMK Studio 建在左半 |

## 驗證步驟（刷機後）

1. 從 GitHub Actions 下載 artifact，**左右兩半都刷**：右半包含 tap driver 修正，左半包含 split queue 與方向修正。
2. 手指在觸控板上「往鍵盤的上方」移動 → 游標應向上；四個方向逐一確認。
3. 手指往右 → 游標應向右。
4. 雙指往上 → 頁面應向下捲動（macOS 自然手勢）。
5. 單指連點至少 20 次，確認每次左鍵都會立即放開；再測雙指點擊（右鍵）與 press-and-hold 拖曳。

## 未來調整建議

### 短期（實測後很可能要動）

- **游標速度**：目前 `&mmv_input_listener` 掛了 `zip_xy_scaler 2 1`（放大 2 倍），且 keymap 開頭 `ZMK_POINTING_DEFAULT_MOVE_VAL 1200`。若游標太快/太慢，優先調 scaler 比例，例如 `zip_xy_scaler 3 2`（1.5 倍）。
- **捲動方向與速度**：速度優先調 `zip_scroll_scaler 2 1`；只有方向不順手時才調 `natural-scroll-x/y` 或 `zip_scroll_transform`。
- **點擊誤觸**：打字時手掌容易掃到觸控板的話，調高 `stationary-threshold`（目前 5）或 `bottom-beta`（目前 5），前者是判定「有在移動」的像素門檻。
- **press-and-hold 時間**：目前 250ms，拖曳視窗覺得太靈敏/太鈍可調 `press-and-hold-time`。

### 中期（品質提升）

- **依層切換觸控板行為**：專案已掛 `zmk-module-runtime-input-processor`（可在 ZMK Studio 執行期調整 input processor），可以做到例如在 Nav 層把觸控板變成捲動模式（`zip_xy_to_scroll_mapper` 類的 processor）。
- **temp-layer（自動切層）**：加 `zip_temp_layer` 讓「一摸觸控板就切到滑鼠層」，該層放左右鍵與捲動鍵，用完自動跳回，是觸控板鍵盤最常見的 QoL 改善。
- **省電確認**：觸控板在 I2C 上常駐，實測右半電池消耗；若明顯變耗電，確認 IQS5xx 的 idle/LP 模式有生效（驅動 RDY pin 中斷驅動，理論上待機電流低，但值得量一次）。

### 長期（架構面）

- **追蹤上游**：目前 ZMK 用的是 cormoran fork `v0.3-branch+dya` 加上多個 cormoran 模組。ZMK 官方的 pointing / input-split 已逐版收斂，建議定期評估能否回到官方 release，減少 fork 維護成本。
- **合併回 main**：觸控板功能驗證穩定後把 trackpad 分支併回 main，並更新 README 的功能說明（加上觸控板段落與手勢一覽）。
- **settings_reset 韌體**：換角色/改綁定後若配對異常，記得 build.yaml 裡已有 `settings_reset` target 可用來清設定。
