# 觸控板整合說明（trackpad 分支）

> 最後更新：2026-07-16
> 硬體：Azoteq IQS5xx 系列觸控板（TPS43），安裝於**右半**，逆時針旋轉 90 度安裝

## 目前狀態

| 項目 | 狀態 |
|---|---|
| 分體角色 | 左半 = central（主控，跑 ZMK Studio）；右半 = peripheral |
| 觸控板位置 | 右半，接 I2C1（SDA=P1.08、SCL=P0.24、RST=P1.04、RDY=P1.07），位址 `0x74` |
| 事件轉送 | 右半透過 `zmk,input-split` 將指標事件轉送到左半 central |
| 方向設定 | Central 端軟體轉換：`zip_xy_transform Y_INVERT`（游標）+ `zip_scroll_transform Y_INVERT`（mac 自然捲動）；晶片旗標全關 |
| 手勢 | 單指點擊=左鍵、雙指點擊=右鍵、press-and-hold（250ms）、雙軸自然捲動 |

## 架構總覽

觸控板事件的路徑：

```
TPS43 (右半 I2C1)
  → iqs5xx 驅動（AYM1607/zmk-driver-azoteq-iqs5xx）
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
- [config/eyelash_corne.conf](../config/eyelash_corne.conf) — `CONFIG_INPUT=y`、`CONFIG_INPUT_AZOTEQ_IQS5XX=y`、`CONFIG_ZMK_POINTING=y`
- [config/west.yml](../config/west.yml) — 掛載 `zmk-driver-azoteq-iqs5xx`（AYM1607）與 cormoran 的 ZMK fork（`v0.3-branch+dya`）

## 方向設定的原理（背景知識）

> ℹ️ 晶片暫存器旗標經最終驗證**是有效的**（中途一度誤判無效，見下一節的過程記錄），但本專案的方向調整統一使用 central 端的 `zip_xy_transform`，晶片旗標保持全關 — 單一真相來源，避免兩層轉換互相疊加造成混亂。

`switch-xy` / `flip-x` / `flip-y` **不是軟體轉換**：驅動在初始化時把這三個旗標寫進觸控板晶片的 `XY_CONFIG_0` 暫存器（位址 `0x0669`），之後晶片直接回報轉換後的座標。

依 Azoteq IQS5xx-B000 datasheet（§5.8 XY Output Flip & Switch、§8.10 暫存器定義），三個旗標的精確語意是：

- `SWITCH_XY_AXIS`（bit 2）：切換 X/Y **輸出**由哪組電極負責（Rx 欄 ↔ Tx 列）
- `FLIP_Y`（bit 1）：翻轉**最終輸出**的 Y 值
- `FLIP_X`（bit 0）：翻轉**最終輸出**的 X 值

**關鍵：flip 作用在交換「之後」的輸出軸上。** 所以調方向的正確思考方式是兩步驟：

1. 游標動的軸不對（手指上下移動游標卻左右動）→ 切換 `switch-xy`
2. 軸對了但某個方向相反 → 游標**左右**相反就切換 `flip-x`、**上下**相反就切換 `flip-y`，兩者互不影響

三個旗標共 8 種組合，其中 4 種是純旋轉（0/90/180/270 度）、4 種是鏡像。若實測發現「只有一個軸相反」，代表需要的是含鏡像的組合，這在感應器安裝面向與預期相反時是正常的。

實務上的另一個關鍵點：**改方向設定後，必須重刷「右半」韌體**。暫存器初始化發生在觸控板所在的右半；只刷左半（含 Studio 版）完全不會生效。

### 方向校正全記錄與最終定案（2026-07-16）

**最終答案：感應器原生方向 = X 與螢幕一致、Y 相反。** 修正只需要反轉 Y：

- 晶片端（`tps43` 節點）：**不設任何方向旗標**（等效的晶片解是只加 `flip-y`，但本專案統一用軟體轉換）
- Central 端（`tps43_input` listener）：

```dts
input-processors = <&zip_xy_transform INPUT_TRANSFORM_Y_INVERT>,
                   <&zip_scroll_transform INPUT_TRANSFORM_Y_INVERT>;
```

`zip_xy_transform` 修游標方向；`zip_scroll_transform` 反轉 REL_WHEEL 讓雙指捲動符合 mac 自然捲動（雙指往上 = 頁面往下）。

**校正過程的曲折與教訓**（按時間順序）：

1. 多輪 flip/switch 旗標迭代結果矛盾，一度誤判「晶片暫存器無效」— 實際原因是**其中一次測試的韌體沒有真的刷進右半**，兩次「不同設定」測到相同行為。
2. 決定性證據：移除 `switch-xy` 的版本刷入右半後，raw 行為從「上→右、右→下」變成「上→下、右→右」，變化量精確等於移除一個軸交換 → **晶片暫存器其實有效**。
3. 由 raw 行為（旗標全關）直接解出感應器原生方向：`M = [[1,0],[0,−1]]`（只有 Y 鏡像），修正 = 反轉 Y，一步到位。
4. 過程中在 processor 掛過 `XY_SWAP | Y_INVERT`（基於「暫存器無效」的錯誤前提推導），刷左半後行為變成「上→右、右→上」，反推出 raw 已改變，才發現第 2 點。

**教訓**：
- 每次測試前，確認要測的那一半**真的刷了新韌體**（方向設定在右半晶片、processor 在左半，改哪層就刷哪半）。
- 兩筆「不同設定、相同行為」的觀察 = 至少有一次刷機沒成功，先驗證刷機再懷疑程式。
- 雙軸測試（手指往上游標往哪 + 手指往右游標往哪）是唯一可靠的觀察格式，「差 90 度」這種描述會因旋轉方向歧義誤導判斷。

### 未來校正 SOP

方向不對時，一律調 `tps43_input` 上的 processor 旗標（**改完刷左半**）；`tps43` 晶片旗標雖然有效，但保持全關、單一真相來源在 processor 層：

1. 做兩個單軸測試：手指往上游標往哪、手指往右游標往哪。
2. 兩軸都對 → 完成。
3. 只有左右相反 → 切換 `INPUT_TRANSFORM_X_INVERT`；只有上下相反 → 切換 `INPUT_TRANSFORM_Y_INVERT`。
4. 差 90 度（上變左右、右變上下）→ 切換 `INPUT_TRANSFORM_XY_SWAP`，重測後用步驟 3 修掉殘餘反向。
5. 全部相反（180 度）→ 同時切換兩個 INVERT。
6. 捲動方向：調 `zip_scroll_transform` 的 `Y_INVERT`（垂直）/`X_INVERT`（水平）。

## 異動紀錄（trackpad 分支）

| Commit | 內容 |
|---|---|
| （未 commit）2026-07-16 | **方向與捲動定案**：解出感應器原生方向為「僅 Y 鏡像」，processor 改為 `zip_xy_transform Y_INVERT`，並新增 `zip_scroll_transform Y_INVERT`（mac 自然捲動）。**此修改需刷左半** |
| `a4a669d` 等五筆 | 2026-07-16 當天的方向迭代（晶片旗標 flip-y / switch-xy / flip-x / switch-xy → processor XY_SWAP\|Y_INVERT），過程含一次「暫存器無效」誤判，完整記錄見「方向校正全記錄」一節 |
| `ea4238d` | 設定順時針 90 度（保留 `switch-xy`、加 `flip-x`）— 已被上列變更取代 |
| `fcfa977` | 加入 `zmk,input-split` 節點，讓 peripheral 的指標事件轉送到 central |
| `2b597f4` | 全域啟用 input subsystem 與 Azoteq IQS5xx 驅動 |
| `57f7768` | 修正分體角色：左半為 central，ZMK Studio 建在左半 |

## 驗證步驟（刷機後）

1. 從 GitHub Actions 下載 artifact，**左右兩半都刷**（至少右半必刷）。
2. 手指在觸控板上「往鍵盤的上方」移動 → 游標應向上；四個方向逐一確認。
3. 若軸不對（上下動變左右動）→ 切換 `switch-xy`。
4. 軸對但方向反 → 左右相反切換 `flip-x`、上下相反切換 `flip-y`（flip 作用在最終輸出軸，兩者獨立，一次就能收斂）。
5. 確認手勢：單指點擊（左鍵）、雙指點擊（右鍵）、雙指捲動方向是否符合直覺（不符合就調 `natural-scroll-x/y`）。

## 未來調整建議

### 短期（實測後很可能要動）

- **游標速度**：目前 `&mmv_input_listener` 掛了 `zip_xy_scaler 2 1`（放大 2 倍），且 keymap 開頭 `ZMK_POINTING_DEFAULT_MOVE_VAL 1200`。若游標太快/太慢，優先調 scaler 比例，例如 `zip_xy_scaler 3 2`（1.5 倍）。
- **捲動方向與速度**：`zip_scroll_scaler 2 1` 同理；方向不順手時調 `natural-scroll-x` / `natural-scroll-y`（在 dtsi 的 `tps43` 節點）。
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
