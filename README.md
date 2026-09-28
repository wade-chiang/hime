# HIME Input Method Editor

![badge](https://github.com/hime-ime/hime/workflows/Build/badge.svg)

## 關於這個 fork：Wayland 支援

這是 [hime-ime/hime](https://github.com/hime-ime/hime) 的個人 fork，目標是讓 HIME 在 Wayland 桌面上原生運作，同時保留 HIME 原本的介面與打字手感。改動只放在這個 fork，不會送回原始專案；歡迎有需要的人使用。

### 目前狀態

- GTK 與 Qt 的 IM module 不再依賴 X11，原生 Wayland 程式可以透過 HIME 輸入，不需要 `GDK_BACKEND=x11`。
- 新增 GTK 4 IM module。
- 在支援 layer-shell 的合成器上（niri、sway、Hyprland、KDE 等），HIME 主程式直接跑在 Wayland 上，視窗以 layer-shell 顯示：固定在設定的位置、不會搶走鍵盤焦點，不需要 Xwayland。
- 在 GNOME 等沒有 layer-shell 的桌面上，HIME 主程式與它的視窗跑在 Xwayland 上。
- 在支援 input-method-v2 的合成器上（niri、sway、Hyprland 等），HIME 主程式同時是 Wayland 的輸入法：使用 text-input 協定的程式不需要 HIME 的 IM module 也能用 HIME 輸入，例如 foot、GTK 4 與 Qt 6 程式（未設定 IM module 時）、GTK 3 程式（`GTK_IM_MODULE=wayland`）、Firefox、Chromium（加上 `--enable-wayland-ime`）。
- 在 GNOME 上，HIME 可以作為 IBus 的輸入來源（`hime-ibus`）：在「設定 → 鍵盤 → 輸入來源」加入 HIME；使用 text-input 的程式不需要 HIME 的 IM module，「跟著游標」時輸入視窗出現在文字游標下方。
- 在 KDE Plasma 上，HIME 可以作為 KWin 的輸入法（input-method-v1）：在「系統設定 → 鍵盤 → 虛擬鍵盤」選擇 HIME，由 KWin 啟動 HIME；使用 text-input 的程式同樣不需要 HIME 的 IM module。
- 啟動時自動選擇；可用 `HIME_BACKEND=x11` 或 `HIME_BACKEND=wayland` 強制指定。
- 以固定位置的輸入視窗（外觀設定 → 固定輸入視窗位置）為主。

驗證範圍：

- 自動測試：`make check-session`，在 headless mutter、sway、KWin 或 GNOME Shell 中以模擬按鍵輸入；sway、KWin 與 GNOME 中另以截圖檢查輸入視窗的位置。
- 真實桌面測試程式：同樣的測試程式，在實際登入的桌面中，以已安裝的套件與嘸蝦米字根表輸入。
- 實際使用：在真實程式中手動打字。

| 程式 | 自動測試 | 真實桌面測試程式 | 實際使用 |
|---|---|---|---|
| GTK 3 | Wayland、X11 | niri 26.04（Wayland） | 尚未 |
| GTK 4 | Wayland、X11 | GNOME 50（Wayland） | Ghostty（GNOME 50、niri 26.04） |
| Qt 6 | Wayland、X11 | niri 26.04（Wayland） | DMS 記事本（Quickshell，niri 26.04）；GNOME 尚未 |
| Qt 5 | 僅 X11 | niri 26.04（經 xwayland-satellite 的 X11） | 尚未；Wayland 未測試 |

HIME 作為 Wayland 輸入法（text-input 程式，不經 HIME module）：

| 程式 | 自動測試 | 實際使用 |
|---|---|---|
| GTK 3（`GTK_IM_MODULE=wayland`） | sway、KWin 6.7、GNOME 50 | 尚未 |
| GTK 4 | sway、KWin 6.7、GNOME 50 | Ghostty（niri 26.04）、gnome-text-editor（GNOME 50） |
| Qt 6（`QT_IM_MODULE=wayland`） | sway、KWin 6.7、GNOME 50 | FeatherPad（niri 26.04） |
| 其他 text-input 程式 | — | foot（niri 26.04、GNOME 50） |

以上程式在 niri 26.04 與 GNOME 50（經 IBus）上也實測過「跟著游標」：HIME 的輸入視窗出現在文字游標下方（Ghostty、foot、gnome-text-editor、FeatherPad）。KDE Plasma 尚未實機測試。

### 安裝

#### Arch Linux

```bash
git clone https://github.com/wade-chiang/hime.git
cd hime/distro/archlinux
makepkg -si -p PKGBUILD-wayland
```

`PKGBUILD-wayland` 會從 GitHub 抓 master 編譯。之後要更新，在同一個目錄先 `git pull`（取得最新的 PKGBUILD），再執行一次 `makepkg -si -p PKGBUILD-wayland`，makepkg 會抓最新的程式碼重新編譯，套件版本號會跟著 master 更新。

- 編譯時若已安裝 anthy、libchewing，會一併編出日文（Anthy）與新酷音模組；裝了 ibus 會編出 GNOME 用的 IBus 引擎。
- 會與官方的 `hime`、`hime-git` 套件衝突，安裝時 pacman 會詢問是否取代。

#### 安裝或更新後

- 重新啟動 HIME：`pkill -x hime; setsid -f hime`（或登出再登入）。已開啟的程式會自動重新連線。
- GNOME：第一次安裝後需重新登入，IBus 才會列出 HIME，再到「設定 → 鍵盤 → 輸入來源」加入。
- KDE Plasma：到「系統設定 → 鍵盤 → 虛擬鍵盤」選擇 HIME。

#### 其他發行版，自行編譯

需要的開發套件：GTK 3、gtk-layer-shell、wayland（含 wayland-scanner）、xkbcommon、libXtst；可選：GTK 4、Qt 5／Qt 6、IBus、anthy、libchewing（有裝的會自動偵測並編譯對應部分）。

```bash
autoreconf -fi
./configure --prefix=/usr --with-gtk=3.0 --disable-system-tray
make
sudo make install
sudo gtk-query-immodules-3.0 --update-cache
sudo gio-querymodules /usr/lib/gtk-4.0/4.0.0/immodules
```

GTK 4 與 Qt 的 module 目錄依發行版而異（例如 Debian 在 `/usr/lib/x86_64-linux-gnu`）；Qt 的位置可以用 `--with-qt5-im-module-path`、`--with-qt6-im-module-path` 指定。沒有 gtk-layer-shell 時，HIME 在 Wayland 上會改在 Xwayland 執行。

### 設定

建立 `~/.config/environment.d/hime.conf`。建議和 fcitx5 相同的設定：GTK 程式與 Qt 6 程式透過合成器輸入（text-input），輸入視窗跟著游標；Qt 5 程式透過 HIME 的 IM module，純 X11 程式透過 XIM（限制見「已知限制」）。

```
QT_IM_MODULE=hime
QT_IM_MODULES=wayland;hime
XMODIFIERS=@im=hime
```

- 不設定 `GTK_IM_MODULE`：GTK 在 Wayland 上會自動使用 text-input。
- `QT_IM_MODULES` 只有 Qt 6 會讀，它會先用 text-input；Qt 5 只讀 `QT_IM_MODULE`，使用 HIME 的 module。

若想讓所有程式都透過 HIME 的 IM module 輸入（和 X11 上一樣，輸入視窗固定位置，每個程式各自記住中英狀態），改用：

```
GTK_IM_MODULE=hime
QT_IM_MODULE=hime
QT_IM_MODULES=hime
XMODIFIERS=@im=hime
```

`environment.d` 只在 systemd 使用者服務啟動時讀取。若登出再登入後沒有生效（例如還有其他登入中的 session），改完檔案後執行下面這行再重新登入（`systemctl --user unset-environment` 移除不了 `environment.d` 設定的變數）：

```bash
systemctl --user daemon-reload
```

HIME 會在第一次透過 IM module 打字時自動啟動；GNOME（IBus）與 KDE Plasma（KWin）也會自動啟動 HIME。照建議設定時，niri 等合成器上多半沒有程式透過 IM module，需要在合成器設定中登入時啟動 HIME，例如 niri 的 `spawn-at-startup "hime"`。

#### 中英狀態

和 fcitx5 一樣，透過合成器輸入的程式各自記住中英狀態與輸入法（透過 IM module 的程式本來就各自記住）：

- niri、sway、Hyprland、labwc、KDE Plasma：每個視窗一份。
- GNOME：每個程式一份。GNOME 只把目前的程式告訴 xdg-desktop-portal-gnome，HIME 照 fcitx5 的做法從中讀取；沒有執行 xdg-desktop-portal-gnome 時，全部共用一份。
- 同一個視窗裡的分頁（瀏覽器、終端機）共用一份。
- 想要全部共用：在 hime-setup 勾選「所有程式共用相同的輸入法狀態」。

#### GNOME

HIME 可以作為 IBus 的輸入來源：到「設定 → 鍵盤 → 輸入來源 → ＋」加入「HIME」，用右上角選單或 Super+Space 切換輸入來源；選了 HIME 之後，Ctrl+Space 照舊切換中英（`hime-init-im-enabled` 設定新欄位一開始是否為中文）。

- 使用 text-input 的程式（GTK 4、foot，以及未設定 IM module 的 GTK 3 與 Qt 程式）經 IBus 輸入；「跟著游標」時輸入視窗出現在文字游標下方。
- 設定 `GTK_IM_MODULE=hime`、`QT_IM_MODULE=hime` 的程式照舊透過 HIME 的 IM module 輸入，輸入視窗固定位置。
- 安裝後需重新登入，IBus 才會列出 HIME。
- 使用分數縮放、或開啟 mutter 的 `xwayland-native-scaling` 時，跟著游標的位置可能有偏差。

GNOME 登入時會把 `QT_IM_MODULE`、`QT_IM_MODULES`、`XMODIFIERS` 設成 IBus 的值，所以 Qt 程式可能仍然使用 IBus；GTK 程式不受影響。

#### niri 及其他支援 layer-shell 的合成器

不需要額外設定。HIME 會自動改用 Wayland 與 layer-shell，niri 也不需要 xwayland-satellite 或視窗規則；舊版說明中的 `app-id="(?i)^hime$"` 視窗規則可以移除。

HIME 也會成為合成器的輸入法，兩種方式可以同時使用：

- 有設定 `GTK_IM_MODULE=hime`、`QT_IM_MODULE=hime` 的程式，照舊透過 HIME 的 IM module 輸入。
- 其他使用 text-input 的程式（foot、Chromium，以及照建議設定時的 GTK 與 Qt 6 程式）直接透過合成器輸入。

Ghostty 預設只跑一個程式實例：已經有 Ghostty 在執行時，用不同環境變數再開，只會在原本的程式裡多開一個視窗，沿用原本的設定。要測試時可加上 `--gtk-single-instance=false`。

同一時間只能有一個輸入法；若 fcitx5 或 IBus 已經佔用，HIME 會顯示「another Wayland input method is running」，只提供 IM module 的方式。設定 `HIME_NO_WAYLAND_IM=1` 可以關閉這個功能。

#### KDE Plasma

在「系統設定 → 鍵盤 → 虛擬鍵盤」選擇「HIME」。KWin 會啟動 HIME，並在 HIME 當掉時重新啟動它；若已經有 HIME 在執行（例如由 IM module 啟動），KWin 啟動的 HIME 會接手，已開的程式會自動重新連線。

- 使用 text-input 的程式（GTK 3/4、Qt 6、foot 等）直接透過 KWin 輸入，「跟著游標」時輸入視窗出現在文字游標下方。
- 設定 `GTK_IM_MODULE=hime`、`QT_IM_MODULE=hime` 的程式照舊透過 HIME 的 IM module 輸入，輸入視窗固定位置。若只想在 Plasma 中改走 text-input，可以把設定寫在 `~/.config/plasma-workspace/env/` 的腳本裡，不影響其他桌面。
- Chromium、Electron 程式需加上 `--enable-wayland-ime --wayland-text-input-version=3`。

### 已知限制

- 「跟著游標」的輸入視窗只在 text-input 程式中（HIME 作為 Wayland 輸入法時）跟著游標；透過 HIME IM module 的 Wayland 程式拿不到游標位置，視窗固定在「固定輸入視窗位置」所設定的座標。詞音的候選字視窗、符號表、新酷音與 Anthy 的視窗也固定在該位置。
- 輸入到一半時在 hime-setup 切換輸入視窗樣式，已打的字根會被清掉。
- 在 Wayland 程式中使用虛擬鍵盤時，Shift、Ctrl 等修飾鍵，以及 Backspace、方向鍵等輸入法不處理的按鍵，無法傳給程式。
- 作為 Wayland 輸入法時，候選字等屬性（底線、反白）不會顯示在程式的預編輯文字中：協定不支援。
- 作為 Wayland 輸入法時，焦點離開輸入欄位時尚未送出的字（例如詞音的整句）會被丟棄：協定不接受失去焦點後送出的文字。
- 輸入法選單（在輸入視窗上按滑鼠中鍵）在 layer-shell 上尚未處理。
- HIME 跑在 Wayland 上時（niri、KDE Plasma 等）不提供 XIM，沒有 GTK/Qt IM module 的純 X11 程式無法使用 HIME。
- GTK 4 程式的 AltGr 修飾鍵不會傳給 HIME。
- Chromium、Electron 程式在原生 Wayland 下尚未測試。

### 開發

```bash
make check           # 字根表引擎的行為比對測試
make check-session   # 在 headless mutter 與 sway 中的端到端測試
```

開發筆記與 Wayland 計畫見 [CLAUDE.md](CLAUDE.md)。

## Hime 新手上路

### 簡介

Hime 是一個極好用的輸入法框架，輕巧、穩定、功能強大且支援許多常用的輸入法，包括

- 倉頡
- 注音
- 大易
- 行列
- 嘸蝦米
- 希臘字母
- 日本 Anthy
- 韓拼
- 拉丁字母
- 亂倉打鳥
- 酷音等....

#### 特色

- 支援多種輸入法, 使用者選擇多
- 支援多種智慧型注音輸入法
- 支援多種拆字型輸入法
- 支援內碼輸入

#### 授權

LGPLv2.1 (Qt immodules are GPLv2)

### 取得/安裝

如果您尚未取得 Hime, 可依下列方法取得 Hime:

#### 穩定版本

##### Debian/Ubuntu 等衍生系統

```bash
apt update
apt install hime
```

#### 最新開發版本

##### 下載原始碼

```bash
git clone https://github.com/hime-ime/hime.git
cd hime
```

以下列出4種不同安裝方式

##### dpkg 安裝 (Debian/Ubuntu)

```bash
distro/debian/gen-deb
sudo dpkg -i ../hime_{version}+git{date}-1_{arch}.deb
```

##### rpm 安裝 (RedHat/Fedora/CentOS)

```bash
distro/fedora/gen-rpm
```

##### pacman 安裝 (Arch Linux)

```bash
cd distro/archlinux
./makepkg.sh
sudo pacman -U hime-git-{version}-{arch}.pkg.tar.zst
```

##### 自行編譯安裝

```bash
autoreconf -i
./configure
make
make install
```

## 開始使用 Hime

### 開啟/切換輸入法

- 按 Ctrl+Space 來開啟輸入視窗
- 按 Ctrl+Shift 來循環切換輸入法

### Hime 問題回報

使用者可以依下列途徑回報問題:

- Github issue tracker
- #hime at irc.freenode.net

## 開發

- 歡迎發送 PR 協助改善 HIME
- 發送 PR 前請先執行 `make clang-format` 以自動修正 coding style (需安裝 `clang-format` 版本 14 `(clang-format-14)`)

## 附錄

### 常見問題

#### "HIME" 怎麼念？

- HIME 在日語中是「姫」，公主的意思，發音為「ひめ」。中文沒有對應的音，英文的發音為: Pronounce "Hi" like the English word "he", then pronounce "me" like the English word "may" or the "Me" in "Merry Christmas"。

#### 為什麼（詞音的）詞庫不會隨著 HIME 更新？

- 目前輸入法的設計並不會更新存放於使用者家目錄的詞庫資料，未來的版本可能會改善，細節請見 issue #136

### 不相容應用程式

目前仍有應用程式無法與 HIME 進行很好的搭配

[不相容列表](https://github.com/hime-ime/hime/wiki/List-of-software-incompatible-with-H.I.M.E.)

### 按鍵功能一覽表

| 按鍵                       | 功能                                                                                                      |
| -------------------------- | --------------------------------------------------------------------------------------------------------- |
| *                          | gtab 輸入法拆碼中代表任意數量字元 非 gtab 輸入法中輸出 * 或 ＊                                            |
| ?                          | gtab 輸入法拆碼中代表任意單一字元 輸出?或？                                                               |
| `                          | gtab 輸入法中開啟同音字選擇視窗                                                                           |
| <                          | 注音輸入法顯示上一頁重覆字                                                                                |
| ’                          | 詞音中輸出全形、符號                                                                                      |
| h                          | 詞音 vi 編輯模式遊標左移一個字元                                                                          |
| l                          | 詞音 vi 編輯模式遊標右移一個字元                                                                          |
| Q                          | 詞音許氏鍵盤排列時可選擇同音字(跟向下鍵一樣)                                                              |
| q                          | 詞音許氏鍵盤排列時可選擇同音字(跟向下鍵一樣)                                                              |
| x                          | 詞音 vi 編輯模式刪除一個字元                                                                              |
| Alt+Shift+按鍵             | 輸出 phrase.table 定義的字串                                                                              |
| Alt+Space                  | 可設定為輸入法狀態切換開關                                                                                |
| BackSpace                  | 清除一個拆碼 清除緩衝區的一個字元                                                                         |
| CapsLock                   | 詞音/日本 anthy 切換中英文狀態                                                                            |
| Ctrl+Alt+Space             | 在輸入視窗畫紅色的 X 用以協助除錯                                                                         |
| Ctrl+Alt+,                 | 符號視窗開關切換                                                                                          |
| Ctrl+Alt+0                 | 切換為內碼輸入法                                                                                          |
| Ctrl+Alt+3                 | 切換為注音輸入法                                                                                          |
| Ctrl+Alt+6                 | 切換為詞音輸入法                                                                                          |
| Ctrl+Alt+=                 | 切換為日本 anthy 輸入法                                                                                   |
| Ctrl+Alt+g                 | 輸出前一次的字串                                                                                          |
| Ctrl+Alt+r                 | 輸出前一次的字串                                                                                          |
| Ctrl+Alt+【-1245789=[\]`】 | 切換為 gtab 輸入法                                                                                        |
| Ctrl+Shift                 | 循環切換輸入法                                                                                            |
| Ctrl+Shift+;               | 在非 XIM 模式輸出全形：符號                                                                               |
| Ctrl+Space                 | 輸入法狀態切換開關                                                                                        |
| Ctrl+e                     | 詞音切換 vi 編輯模式                                                                                      |
| Ctrl+u                     | 清除詞音緩衝區                                                                                            |
| Ctrl+按鍵                  | 輸出 phrase-ctrl.table 定義的字串                                                                         |
| Delete                     | 刪除緩衝區遊標所在的字 刪除內碼輸入法的一個拆碼                                                           |
| Down                       | 詞音中選擇同音字                                                                                          |
| End                        | 關閉選擇視窗並移動到緩衝區末端                                                                            |
| Enter                      | 輸出緩衝區內容                                                                                            |
| Escape                     | 清除所有拆碼 關閉gtab同音字選擇視窗                                                                       |
| F11                        | 日本 anthy 輸入法中呼叫 kasumi 管理模式                                                                   |
| F12                        | 日本 anthy 輸入法中呼叫 kasumi 加詞模式(先圈選想加的詞再按 F12)                                           |
| Home                       | 關閉選擇視窗並移動到緩衝區最前端                                                                          |
| Left                       | 關閉選擇視窗並在緩衝區左移一個字元                                                                        |
| PageDown                   | gtab/日本 anthy                                                                                           |
| PageUp                     | gtab/日本 anthy                                                                                           |
| 數字盤的+鍵                | gtab 輸入法顯示下一頁重覆字(若在末頁則回到第一頁)                                                         |
| 數字盤的-鍵                | gtab 輸入法顯示上一頁重覆字(到第一頁為止)                                                                 |
| Right                      | 關閉選擇視窗並在緩衝區右移一個字元                                                                        |
| Shift                      | 可設定為詞音/日本 anthy 切換中英文狀態                                                                    |
| Shift+Enter                | 詞音新增詞(從遊標所在位置到緩衝區末端)                                                                    |
| Shift+Space                | 全形狀態切換開關 可設定為輸入法狀態切換開關                                                               |
| Shift+按鍵                 | 可設定在 gtab 狀態中取代「Alt+Shift+按鍵」                                                                |
| Shift+數字                 | 詞音中選擇候選詞                                                                                          |
| Shift+標點                 | 詞音中輸出全形標點符號                                                                                    |
| Space                      | 拆碼輸入完成顯示候選字 翻頁顯示重覆字 輸出第一個候選字<br>輸出半形或全形空白 注音及詞音狀態表示音調的一聲 |
| Tab                        | 可在詞音中代替 Enter 鍵輸出緩衝區內容 可設定為詞音切換中英文狀態                                          |
| Up                         | 詞音中選擇近音字                                                                                          |
| Windows+Space              | 可設定為輸入法狀態切換開關                                                                                |
| 滑鼠左鍵                   | 符號視窗開關切換 系統列半形全形開關切換                                                                   |
| 滑鼠中鍵                   | 選擇輸入法                                                                                                |
| 滑鼠右鍵                   | 顯示系統列選單                                                                                            |
| 滑鼠滾輪往上               | 符號視窗循環切換成上一組符號表(滑鼠遊標須在符號視窗範圍內)                                                |
| 滑鼠滾輪往下               | 符號視窗循環切換成下一組符號表(滑鼠遊標須在符號視窗範圍內)                                                |

### 參考文獻

[取得/安裝](https://github.com/hime-ime/hime/wiki/Prebuilt-packages-for-Linux-distributions)
