# Makefile

# コンパイラ
CXX := g++

# ソースファイル
SRC := pyramid.cpp

# 出力ファイル
TARGET := pyramid

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

