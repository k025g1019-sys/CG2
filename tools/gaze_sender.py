#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
視線パケット送信プログラム（別プロセス → ゲーム本体）。

ゲーム本体（EyeTracker）が読む「共有ファイル（メモリマップ）」へ、視線/頭の正規化位置を
書き込み続ける。OpenGaze 本体を立ち上げる前の動作確認や、軽量な代替プロデューサとして使う。
OpenGaze を使う場合も「同じ共有ファイルへ同じ 28 バイトのパケットを書く」だけで差し替え可能。
共有先は既定で %TEMP%\\DirectXGameGaze.bin（環境変数 DIRECTXGAME_GAZE_FILE で変更可）。

パケット（Engine/Input/GazePacket.h と一致させること）:
    struct GazePacket {            # pack(1)
        uint32 magic;   # 0x47415A45 ('GAZE')
        uint32 valid;   # 0=無効 / 1=有効
        uint64 frameId; # 更新ごとに +1
        float  gazeX;   # [-1..+1]  -1=左 / +1=右
        float  gazeY;   # [-1..+1]  -1=下 / +1=上
        float  headZ;   # [-1..+1]  奥行き（任意）
    }

使い方:
    python gaze_sender.py                      # 内蔵カメラ＋MediaPipeで頭追跡（既定）
    python gaze_sender.py --source mouse       # カメラ不要。マウス位置を視線に見立てて送る（テスト用）
    python gaze_sender.py --show               # カメラ映像のプレビューを表示
    python gaze_sender.py --invert-x           # 左右の向きが逆なら反転

依存（mediapipeモードのみ）:  pip install opencv-python mediapipe
mouseモードは標準ライブラリだけで動く。
"""

import argparse
import ctypes
import mmap
import os
import struct
import tempfile
import time

# Engine/Input/GazePacket.h と一致させる定数
SHARED_FILE_NAME = "DirectXGameGaze.bin"
SHARED_FILE_ENV = "DIRECTXGAME_GAZE_FILE"
SHARED_MEMORY_SIZE = 64
MAGIC = 0x47415A45
PACKET_FORMAT = "<IIQfff"  # magic, valid, frameId, gazeX, gazeY, headZ


def clamp(v, lo, hi):
    return lo if v < lo else (hi if v > hi else v)


def resolve_shared_path(args):
    """共有ファイルのパスを決める（受信側 EyeTracker と同じ規則）."""
    if args.shared_file:
        return args.shared_file
    env = os.environ.get(SHARED_FILE_ENV)
    if env:
        return env
    return os.path.join(tempfile.gettempdir(), SHARED_FILE_NAME)


class SharedMemoryWriter:
    """ゲーム本体と共有する「ファイルのメモリマップ」への書き込み口."""

    def __init__(self, path):
        # ゲーム本体（EyeTracker）と同じファイルを開き、メモリマップして書き込む。
        # 名前付きカーネルオブジェクトと違い、セッション/権限の違いに強くパスで一致を確認できる。
        self.path = path
        flags = os.O_RDWR | os.O_CREAT
        if hasattr(os, "O_BINARY"):
            flags |= os.O_BINARY
        self.fd = os.open(path, flags)
        if os.fstat(self.fd).st_size < SHARED_MEMORY_SIZE:
            os.ftruncate(self.fd, SHARED_MEMORY_SIZE)
        self.mm = mmap.mmap(self.fd, SHARED_MEMORY_SIZE, access=mmap.ACCESS_WRITE)
        self.frame_id = 0
        self._last_log = 0.0

    def write(self, gaze_x, gaze_y, head_z, valid):
        self.frame_id += 1
        gx = float(clamp(gaze_x, -1.0, 1.0))
        gy = float(clamp(gaze_y, -1.0, 1.0))
        gz = float(clamp(head_z, -1.0, 1.0))
        packet = struct.pack(
            PACKET_FORMAT, MAGIC, 1 if valid else 0, self.frame_id, gx, gy, gz)
        self.mm.seek(0)
        self.mm.write(packet)
        # 1秒ごとに状況を表示（ゲーム側の Diag と突き合わせて確認できる）
        now = time.time()
        if now - self._last_log >= 1.0:
            self._last_log = now
            print(f"  writing frameId={self.frame_id} gaze=({gx:+.2f},{gy:+.2f}) valid={int(valid)}")

    def close(self):
        try:
            self.mm.close()
        except Exception:
            pass
        try:
            os.close(self.fd)
        except Exception:
            pass


def run_mouse(writer, args):
    """マウスカーソル位置を視線に見立てて送る（カメラ不要のテストモード）."""
    user32 = ctypes.windll.user32
    user32.SetProcessDPIAware()
    screen_w = user32.GetSystemMetrics(0)
    screen_h = user32.GetSystemMetrics(1)
    print(f"[mouse] screen = {screen_w}x{screen_h}  (move the mouse to drive the camera)")

    # GetCursorPos に渡す POINT 構造体（ctypes.wintypes の明示インポートを避けて自前定義）
    class POINT(ctypes.Structure):
        _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]
    pt = POINT()

    period = 1.0 / max(1, args.rate)
    while True:
        user32.GetCursorPos(ctypes.byref(pt))
        nx = pt.x / screen_w  # 0..1
        ny = pt.y / screen_h  # 0..1
        gaze_x = (nx - 0.5) * 2.0 * args.gain
        gaze_y = (0.5 - ny) * 2.0 * args.gain  # 上が +Y
        if args.invert_x:
            gaze_x = -gaze_x
        if args.invert_y:
            gaze_y = -gaze_y
        writer.write(gaze_x, gaze_y, 0.0, valid=True)
        time.sleep(period)


def ensure_face_model(args):
    """FaceLandmarkerの.taskモデルを用意する（無ければGoogleの配布元からダウンロード）。パスを返す."""
    import urllib.request
    if args.model:
        return args.model
    model_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "face_landmarker.task")
    if not os.path.exists(model_path):
        url = ("https://storage.googleapis.com/mediapipe-models/face_landmarker/"
               "face_landmarker/float16/1/face_landmarker.task")
        print(f"[mediapipe] モデルを初回ダウンロード中: {url}")
        urllib.request.urlretrieve(url, model_path)
        print(f"[mediapipe] 保存しました: {model_path}")
    return model_path


def run_mediapipe(writer, args):
    """内蔵カメラ＋MediaPipe FaceLandmarker(Tasks API)で頭（鼻先）の画面内位置を視線として送る."""
    try:
        import cv2
        import mediapipe as mp
        from mediapipe.tasks import python as mp_tasks
        from mediapipe.tasks.python import vision
    except ImportError as e:
        print(f"[mediapipe] 依存が見つかりません: {e}")
        print("            py -m pip install opencv-python mediapipe")
        print("            もしくは: py gaze_sender.py --source mouse")
        return

    model_path = ensure_face_model(args)

    cap = cv2.VideoCapture(args.camera_index, cv2.CAP_DSHOW)
    if not cap.isOpened():
        print(f"[mediapipe] カメラ({args.camera_index})を開けませんでした。")
        print("            ゲームの『Show Real Camera』がONだとカメラを取り合って開けないことがあります。")
        return

    # FaceLandmarker（VIDEOモード。1顔のみ）
    options = vision.FaceLandmarkerOptions(
        base_options=mp_tasks.BaseOptions(model_asset_path=model_path),
        running_mode=vision.RunningMode.VIDEO,
        num_faces=1)
    landmarker = vision.FaceLandmarker.create_from_options(options)

    NOSE_TIP = 1  # FaceMeshトポロジの鼻先ランドマーク
    print("[mediapipe] tracking... (プレビュー時は q キー / Ctrl+C で終了)")
    period = 1.0 / max(1, args.rate)
    ts_ms = 0  # detect_for_video は単調増加のタイムスタンプ(ms)が必要

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                time.sleep(period)
                continue

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
            ts_ms = max(ts_ms + 1, int(time.perf_counter() * 1000))
            result = landmarker.detect_for_video(mp_image, ts_ms)

            if result.face_landmarks:
                lm = result.face_landmarks[0][NOSE_TIP]
                # lm.x,lm.y は画像内の正規化座標[0..1]。カメラは自分に向くため左右は反転している。
                # 頭を自分の右へ動かすと画像内では左へ動く → (0.5 - x) で「右が +」。
                gaze_x = (0.5 - lm.x) * 2.0 * args.gain
                gaze_y = (0.5 - lm.y) * 2.0 * args.gain  # 上が +Y
                if args.invert_x:
                    gaze_x = -gaze_x
                if args.invert_y:
                    gaze_y = -gaze_y
                writer.write(gaze_x, gaze_y, 0.0, valid=True)

                if args.show:
                    h, w = frame.shape[:2]
                    cv2.circle(frame, (int(lm.x * w), int(lm.y * h)), 5, (0, 255, 0), -1)
            else:
                # 顔が見つからない間は valid=0（ゲーム側は中央へ戻る）
                writer.write(0.0, 0.0, 0.0, valid=False)

            if args.show:
                cv2.imshow("gaze_sender (press q to quit)", frame)
                if cv2.waitKey(1) & 0xFF == ord("q"):
                    break

            time.sleep(period)
    finally:
        cap.release()
        landmarker.close()
        if args.show:
            cv2.destroyAllWindows()


def main():
    parser = argparse.ArgumentParser(description="視線パケット送信（共有メモリ経由）")
    parser.add_argument("--source", choices=["mediapipe", "mouse"], default="mediapipe",
                        help="視線の取得元（既定: mediapipe）")
    parser.add_argument("--camera-index", type=int, default=0, help="カメラ番号（既定: 0）")
    parser.add_argument("--gain", type=float, default=1.6,
                        help="視線→正規化値の感度。大きいほど少しの動きで大きく振れる")
    parser.add_argument("--invert-x", action="store_true", help="左右を反転")
    parser.add_argument("--invert-y", action="store_true", help="上下を反転")
    parser.add_argument("--show", action="store_true", help="カメラ映像をプレビュー表示（mediapipe）")
    parser.add_argument("--model", default=None,
                        help="FaceLandmarkerの.taskモデルパス（未指定なら自動DL）")
    parser.add_argument("--rate", type=int, default=60, help="送信レート(Hz)")
    parser.add_argument("--shared-file", default=None,
                        help="共有ファイルのパスを明示指定（既定: %TEMP%\\DirectXGameGaze.bin）")
    args = parser.parse_args()

    path = resolve_shared_path(args)
    writer = SharedMemoryWriter(path)
    print(f"shared file: {path}")
    print("↑ このパスがゲーム内 Diag の path と一致しているか確認してください。")
    print(f"source={args.source}  (Ctrl+C で終了)")
    try:
        if args.source == "mouse":
            run_mouse(writer, args)
        else:
            run_mediapipe(writer, args)
    except KeyboardInterrupt:
        pass
    finally:
        # 終了時は無効パケットを書いてゲーム側を中央へ戻す
        writer.write(0.0, 0.0, 0.0, valid=False)
        writer.close()
        print("\n終了しました。")


if __name__ == "__main__":
    main()
