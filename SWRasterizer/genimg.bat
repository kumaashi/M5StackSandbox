ffmpeg -vcodec png -i m5stack.png -vcodec rawvideo -f rawvideo -pix_fmt rgb565 M5Stack.raw
xxd -i M5Stack.raw  > m5stack_tex.h
