cmd_drivers/video/built-in.o :=  arm-linux-ld -EL    -r -o drivers/video/built-in.o drivers/video/fb_notify.o drivers/video/fb.o drivers/video/console/built-in.o drivers/video/logo/built-in.o drivers/video/backlight/built-in.o drivers/video/display/built-in.o drivers/video/cfbfillrect.o drivers/video/cfbcopyarea.o drivers/video/cfbimgblt.o drivers/video/s3c2410fb.o drivers/video/output.o 
