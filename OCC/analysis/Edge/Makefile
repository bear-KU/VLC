# Makefile

# コンパイラ
CXX := g++

# ソースファイル
SRC := edge_detect.cpp

# 出力ファイル
TARGET := edge_detect

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

