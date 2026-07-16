# 觸控板整合說明（trackpad 分支）

> 最後更新：2026-07-16
> 硬體：Azoteq IQS5xx 系列觸控板（TPS43），安裝於**右半**，逆時針旋轉 90 度安裝

## 目前狀態

| 項目 | 狀態 |
|---|---|
| 分體角色 | 左半 = central（主控，跑 ZMK Studio）；右半 = peripheral |
| 觸控板位置 | 右半，接 I2C1（SDA=P1.08、SCL=P0.24、RST=P1.04、RDY=P1.07），位址 `0x74` |
| 事件轉送 | 右半透過 `zmk,input-split` 將指標事件轉送到左半 central |
| 方向設定 | 僅 `flip-x`（不加 `switch-xy`）— 實測「switch-xy 差順時針 90 度」後推導，**待刷機驗證** |
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

## 方向設定的原理（重要）

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

### 本鍵盤的實測推導（2026-07-16）

| 設定 | 實測結果 |
|---|---|
| `switch-xy` + `flip-x` | 回報為「上下正確、左右相反」（此筆觀察可能不可靠，見下註） |
| `switch-xy` | 實測整體差**順時針 90 度** |
| `flip-x`（最終採用） | 對 `switch-xy` 再套順時針 90 度：`(x,y)→(−y,x)`，交換態反轉、X 輸出反向 |

註：各次回報之間有矛盾（`switch-xy` 與 `switch-xy+flip-x` 只差 X 方向，不可能一個差 90 度、一個只反左右），推測部分早期觀察是在韌體未刷進右半的狀態下做的。**以最新一次實測（`switch-xy` 差順時針 90 度）為準。**

若刷了 `flip-x` 後方向「上下左右全部相反」（差 180 度），表示順逆時針判斷相反，改成僅 `flip-y` 即可。

## 異動紀錄（trackpad 分支）

| Commit | 內容 |
|---|---|
| （未 commit）2026-07-16 | 方向多次迭代後定為僅 `flip-x`：實測「僅 `switch-xy`」整體差順時針 90 度，套用旋轉推導為不交換軸 + X 輸出反向（過程紀錄見上方實測推導表） |
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
