# 顔/視線追跡連携と実カメラ表示

ノートPCの内蔵カメラを使った顔（頭）追跡で、ゲーム内カメラを連動させ（頭連動オフアクシス投影）、
物体を立体的な角度から見やすくする機能。あわせて、実カメラ映像を画面右半分に表示できる。

視線の取得元は2通りあり、ImGuiの `Eye Tracking & Camera` ウィンドウで切り替える:

1. **アプリ内の顔検出（既定）** — 外部プロセス不要。内蔵カメラのフレームに Windows 標準の
   顔検出（`Windows.Media.FaceAnalysis.FaceDetector`）をかけ、顔の画面内位置でカメラを動かす。
2. **共有メモリ（外部 OpenGaze 等）** — 別プロセスが書き込む視線パケットを読む（下記の従来方式）。

## 構成

```
[アプリ内・既定]
  内蔵カメラ --(Media Foundation)--> [CameraCapture ワーカー] --(BGRAフレーム)--> [FaceTracker]
                                                                              → 頭連動オフアクシスでカメラを動かす

[外部・代替（OpenGaze 差し替え用）]
  [視線推定プロセス] --(共有ファイルのメモリマップ: 28バイトのGazePacket)--> [ゲーム本体 EyeTracker]
   OpenGaze / gaze_sender.py

[実カメラ表示]  [CameraCapture] --(Media Foundation)--> 内蔵カメラ → 右半分に表示
```

- **アプリ内の顔検出**は [`Engine/Input/FaceTracker.h`](../Engine/Input/FaceTracker.h)。内蔵カメラ1台を
  「表示」と「顔検出」で共用するため、外部の視線プロセスと取り合う問題が起きない。外部依存も増えない。
- **共有メモリ方式**は **共有ファイルのメモリマップ**でやり取りする（既定: `%TEMP%\DirectXGameGaze.bin`、64バイト）。
  名前付きカーネルオブジェクトと違いセッション/権限の食い違いに強く、パスを目視確認できる。
  環境変数 `DIRECTXGAME_GAZE_FILE` で送信側・受信側ともにパスを上書きできる。
  パケット定義は [`Engine/Input/GazePacket.h`](../Engine/Input/GazePacket.h)。送信側と必ず一致させる。
- 実カメラ表示は Media Foundation でゲーム本体が直接取得する（外部依存を増やさない）。

## ゲーム側の操作（ImGui）

`Eye Tracking & Camera` ウィンドウ:

1. **Eye Tracking (gaze-linked camera)** — 顔/視線でゲーム内カメラを連動（初期値 OFF）。
   横の表示が `[connected]` になっていれば取得元から値を受け取れている。
2. **取得元ラジオボタン** — `In-app face (webcam)`（既定）/ `Shared memory (external)` を切替。
   - In-app は `In-app Diag: ready / face / count` で顔検出の状態を確認できる
     （`ready=yes` で検出器起動済み、`face=1` で顔検出中、`count` は検出顔数）。
   - Shared memory は `Diag: shm / magic / valid / frameId` と `path:` で外部送信側との配線を確認できる。
3. **Show Real Camera (split screen)** — ON でゲームを左半分、実カメラを右半分に分割表示（初期値 OFF）。
   横の表示が `[camera ready]` ならカメラを取得できている。

連動の強さは `Camera, DirectionalLight` ウィンドウの **Gaze Move X / Y** で調整できる。
効果が分かりにくいときは同ウィンドウの **Convergence** を被写体距離からずらすと運動視差が見やすくなる。

## アプリ内の顔検出（既定・外部プロセス不要）

`In-app face (webcam)` を選び、**Eye Tracking** を ON にするだけ。内蔵カメラが自動起動し、顔の位置で
ゲーム内カメラが連動する（`Show Real Camera` が OFF でも顔検出だけは動く）。初回は検出器の初期化で
わずかに遅れて `ready=yes` になる。顔が画面に入っていないと `face=0` で中央に戻る。

## 送信プログラム（外部・代替方式の動作確認用）

外部 OpenGaze の差し替え用に共有メモリ方式も残してある。`Shared memory (external)` を選んだ場合に使う。
`gaze_sender.py` が視線パケットを共有メモリへ書き込む。ゲーム本体を起動した状態で実行する。

```powershell
# カメラ不要のテスト（マウス位置を視線に見立てる）。まずこれで配線確認するのが確実
py tools\gaze_sender.py --source mouse

# 内蔵カメラ + MediaPipe FaceLandmarker で頭（鼻先）を追跡
py -m pip install opencv-python mediapipe
py tools\gaze_sender.py --source mediapipe --show
```

`--source mediapipe` の初回は FaceLandmarker のモデル（`tools\face_landmarker.task`, 約3.8MB）を
自動ダウンロードして以後キャッシュする。`mediapipe 0.10.x` は Tasks API（FaceLandmarker）を使う。

主なオプション: `--gain 1.6`（感度）, `--invert-x` / `--invert-y`（向き反転）,
`--camera-index 0`（カメラ番号）, `--rate 60`（送信Hz）, `--model パス`（モデルを明示）。

> まず `--source mouse` で「視線ON時にマウスを動かすとゲーム内カメラが動く」ことを確認すると、
> カメラ/MediaPipe のセットアップ前に IPC 配線だけを切り分けて検証できる。

## OpenGaze 本体に差し替える

`gaze_sender.py` は OpenGaze のスタンドインで、IPC 契約（共有ファイル + パケット形式）を定義している。
OpenGaze 側の出力を、同じ共有ファイル（既定 `%TEMP%\DirectXGameGaze.bin`）へ同じ28バイトの `GazePacket`
（`magic, valid, frameId, gazeX, gazeY, headZ`）で書き込むアダプタを用意すれば、そのまま置き換えられる。

- `gazeX`: -1=左 / +1=右、`gazeY`: -1=下 / +1=上（画面に対する頭/視線の正規化位置）
- `frameId` は更新ごとに +1。ゲーム側はこれが進まなくなると「切断」とみなし中央へ戻す。

## カメラの同時利用について

内蔵カメラ1台を「視線推定プロセス」と「ゲーム本体の実カメラ表示」が同時に開く構成。
Windows のフレームサーバ経由なら通常は同時オープンできるが、ドライバによっては片方が
`[no camera]` になることがある。その場合は一方のみ使うか、視線側を別カメラにする。
```
