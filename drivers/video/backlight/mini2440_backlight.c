#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/miscdevice.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <mach/regs-clock.h>
#include <plat/regs-timer.h>
#include <mach/regs-gpio.h>
#include <linux/cdev.h>

#define DEVICE_NAME "backlight" //设备名称
#define DEVICE_MINOR 5 //次设备号，这里我们将设备注册为misc设备，这种设备的主设备号都为10

extern void s3c2410_gpio_setpin(unsigned int pin, unsigned int to);
extern void s3c2410_gpio_cfgpin(unsigned int pin, unsigned int function);

static int mini2440_backlight_ioctl(struct inode *inode, 
                                  struct file *file, 
                                  unsigned int cmd, 
                                  unsigned long arg)
{
   switch(cmd)
     {
        case 0:

            //当接收的命令为0时，就将GPG4引脚设为低电平，关闭背光
            s3c2410_gpio_setpin(S3C2410_GPG(4), 0); 
            printk(DEVICE_NAME " turn off!\n");
            return 0;
        case 1:

            //当接收的命令为1时，就将GPG4引脚设为高电平，开启背光
            s3c2410_gpio_setpin(S3C2410_GPG(4), 1); 
            printk(DEVICE_NAME " turn on!\n");
            return 0;
        default:
            return -EINVAL;
     }
}

static struct file_operations dev_fops = 
{
    .owner = THIS_MODULE,
    .ioctl = mini2440_backlight_ioctl, //这里只使用控制IO口的方式来控制背光
};

static struct miscdevice misc =
{
    .minor = DEVICE_MINOR,
    .name = DEVICE_NAME,
    .fops = &dev_fops,
};

static int __init dev_init(void)
{
   int ret;
   ret = misc_register(&misc); //注册成misc设备
   if(ret < 0)
     {
        printk("Register misc device fiald!");
        return ret;
     }

     //将GPG4口配置成输出口
    s3c2410_gpio_cfgpin(S3C2410_GPG(4), S3C2410_GPIO_OUTPUT); 
    s3c2410_gpio_setpin(S3C2410_GPG(4), 1);
    return ret;
}

static void __exit dev_exit(void)
{
    misc_deregister(&misc); //注销该misc设备
}

module_init(dev_init);
module_exit(dev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kaylor");
MODULE_DESCRIPTION("Backlight control for mini2440");
