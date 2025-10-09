CC = gcc
CFLAGS = -Wall -g `pkg-config --cflags libpulse`
LIBS = `pkg-config --libs libpulse` -lpthread

TARGET = wifi_hp_app

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGET)
