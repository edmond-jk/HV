CFLAGS = -O2

all: chardrv_test_app chardrv_regression

chardrv_test_app: chardrv_test_app.o
	$(CC) $(CFLAGS) -o ../chardrv_test_app chardrv_test_app.o

chardrv_regression: chardrv_regression.o
	$(CC) $(CFLAGS) -o ../chardrv_regression chardrv_regression.o
 
.PHONY: clean

clean:
	rm -f *.o *~ core test
