# コンパイラ
CXX := g++

# ソースファイル
SRC := detect.cpp tracker_thread.cpp read_frame.cpp

# 基本のコンパイルフラグ
CXXFLAGS := -std=c++17 -O3 -pthread

# OpenCV & FFmpeg フラグ
LIBS := $(shell pkg-config --cflags --libs opencv4) -lavformat -lavcodec -lswscale -lavutil

# デフォルトターゲット
all: detect

# 描画なし (デフォルト)
detect: $(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) -o detect $(LIBS)

# 描画あり
view: $(SRC)
	$(CXX) $(CXXFLAGS) -DENABLE_GUI $(SRC) -o view $(LIBS)

# クリーンアップ (両方のファイルを消す)
clean:
	rm -f detect view
