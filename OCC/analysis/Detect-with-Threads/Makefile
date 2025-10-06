# Makefile

# コンパイラ
CXX := g++

# ソースファイル
SRC := detect.cpp tracker_thread.cpp

# 出力ファイル
TARGET := detect

# OpenCVフラグ
OPENCV_FLAGS := $(shell pkg-config --cflags --libs opencv4)

# デフォルトターゲット
all: $(TARGET)

# コンパイルルール
$(TARGET): $(SRC)
	$(CXX) $(SRC) -o $(TARGET) $(OPENCV_FLAGS)

# クリーンアップ
clean:
	rm -f $(TARGET)

