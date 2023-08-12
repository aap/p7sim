all: p7sim p7simES

p7sim: main.c glad/glad.o
	cc -o $@ -g $^ -lm -ldl `sdl2-config --cflags --libs` -lSDL2_net
p7simES: main.c glad/glad.o
	cc -o $@ -g -DGLES $^ -lm -ldl `sdl2-config --cflags --libs` -lSDL2_net
