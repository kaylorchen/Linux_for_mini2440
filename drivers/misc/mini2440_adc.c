#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <mach/regs-clock.h>
#include <plat/regs-timer.h>
#include <plat/regs-adc.h>
#include <mach/regs-gpio.h>
#include <linux/cdev.h>
#include <linux/miscdevice.h>

//;自己定义的头文件，因原生内核并没有包含
#include "s3c24xx-adc.h"

#undef DEBUG
//#define DEBUG
#ifdef DEBUG
#define DPRINTK(x...) {printk(__FUNCTION__"(%d): ",__LINE__);printk(##x);}
#else
#define DPRINTK(x...) (void)(0)
#endif

//;定义ADC 转换设备名称，将出现在/dev/adc
#define DEVICE_NAME "adc"

static void __iomem *adc_base; /*定义了一个用来保存经过虚拟映射后的内存地址*/

//;定义ADC 设备结构
typedef struct {
 wait_queue_head_t wait;
 int channel;
 int prescale;
}ADC_DEV;
static ADC_DEV adcdev;

//;声明全局信号量，以便和触摸屏驱动程序共享A/D 转换器
DECLARE_MUTEX(ADC_LOCK);

//;ADC驱动是否拥有A/D 转换器资源的状态变量
//static volatile int OwnADC = 0;

/*用于标识AD转换后的数据是否可以读取，0表示不可读取*/
static volatile int ev_adc = 0;

/*用于保存读取的AD转换后的值，该值在ADC中断中读取*/
static int adc_data;

/*保存从平台时钟队列中获取ADC的时钟*/
static struct clk *adc_clk;

//;定义ADC 相关的寄存器
#define ADCCON (*(volatile unsigned long *)(adc_base + S3C2410_ADCCON)) //ADC control
#define ADCTSC (*(volatile unsigned long *)(adc_base + S3C2410_ADCTSC)) //ADC touch screen control
#define ADCDLY (*(volatile unsigned long *)(adc_base + S3C2410_ADCDLY)) //ADC start or IntervalDelay
#define ADCDAT0 (*(volatile unsigned long *)(adc_base + S3C2410_ADCDAT0)) //ADC conversion data 0
#define ADCDAT1 (*(volatile unsigned long *)(adc_base + S3C2410_ADCDAT1)) //ADC conversion data 1
#define ADCUPDN (*(volatile unsigned long *)(adc_base + 0x14)) //Stylus Up/Down interrupt status
#define PRESCALE_DIS (0 << 14)
#define PRESCALE_EN (1 << 14)
#define PRSCVL(x) ((x) << 6)
#define ADC_INPUT(x) ((x) << 3)
#define ADC_START (1 << 0)
#define ADC_ENDCVT (1 << 15)

//;定义“开启AD 输入”宏，因为比较简单，故没有做成函数

//#define START_ADC_AIN(ch, prescale) 
#define start_adc(ch, prescale) \
do{ \
 ADCCON = PRESCALE_EN | PRSCVL(prescale) | ADC_INPUT((ch)) ; \
 ADCCON |= ADC_START; \
}while(0)
/*设置ADC控制寄存器，开启AD转换*/
/*static void start_adc(int ch,int prescale)
{
    unsigned int tmp;

    tmp = PRESCALE_EN | PRSCVL(prescale) | ADC_INPUT(ch); //(1 << 14)|(255 << 6)|(0 << 3);// 0 1 00000011 000 0 0 0 
 //此处writl()的原型是void writel(u32 b, volatile void __iomem *addr),addr是经过地址重映射后的地址
    writel(tmp, ADCCON); //AD预分频器使能、模拟输入通道设为AIN0

    tmp = readl(ADCCON);
    tmp = tmp | ADC_START; //(1 << 0);   // 0 1 00000011 000 0 0 1 
    writel(tmp, ADCCON);   //AD转换开始
}
问题：此函数被调用时为什么地址映射错误？答案应该需要使用专用的函数iowrite32操作。
*/

//;ADC 中断处理函数
static irqreturn_t adc_irq(int irq, void *dev_id)
{
 //;如果ADC 驱动拥有“A/D 转换器”资源，则从ADC 寄存器读取转换结果
 if (!ev_adc) 
 {
  /*读取AD转换后的值保存到全局变量adc_data中，S3C2410_ADCDAT0定义在regs-adc.h中，
           这里为什么要与上一个0x3ff，很简单，因为AD转换后的数据是保存在ADCDAT0的第0-9位，
           所以与上0x3ff(即：1111111111)后就得到第0-9位的数据，多余的位就都为0*/
  adc_data = ADCDAT0 & 0x3ff;

  /*将可读标识为1，并唤醒等待队列*/
  ev_adc = 1;
  wake_up_interruptible(&adcdev.wait);
 }
 return IRQ_HANDLED;
}

//;ADC 读函数，一般对应于用户层/应用层的设备读函数(read)
static ssize_t adc_read(struct file *filp, char *buffer, size_t count, loff_t *ppos)
{
 
 /*试着获取信号量(即：加锁)*/
 if (down_trylock(&ADC_LOCK)) 
 {
  return -EBUSY;
 }
 if(!ev_adc) /*表示还没有AD转换后的数据，不可读取*/
     {
      if(filp->f_flags & O_NONBLOCK)
         {
              /*应用程序若采用非阻塞方式读取则返回错误*/
         return -EAGAIN;
         }
      else /*以阻塞方式进行读取*/
         {
              /*设置ADC控制寄存器，开启AD转换*/
         start_adc(adcdev.channel, adcdev.prescale);

              /*使等待队列进入睡眠*/
         wait_event_interruptible(adcdev.wait, ev_adc);
         }
     }
 /*能到这里就表示已有AD转换后的数据，则标识清0，给下一次读做判断用*/
   ev_adc = 0;

     /*将读取到的AD转换后的值发往到上层应用程序*/
   copy_to_user(buffer, (char *)&adc_data, sizeof(adc_data));

     /*释放获取的信号量(即：解锁)*/
   up(&ADC_LOCK);

   return sizeof(adc_data);
 
}

//;打开ADC设备的函数，一般对应于用户态程序的open
static int adc_open(struct inode *inode, struct file *filp)
{
 int ret; 
 /* normal ADC */
 ADCTSC = 0;
 //;初始化中断队列
 init_waitqueue_head(&(adcdev.wait));
 adcdev.channel=0;//;缺省通道为“0”
 adcdev.prescale=0xff;
 /* 申请ADC中断服务，这里使用的是共享中断:IRQF_SHARED,为什么要使用共享中断，因为在触摸屏驱动中
      也使用了这个中断号。中断服务程序为:adc_irq在下面实现，IRQ_ADC是ADC的中断号，这里注意：
      申请中断函数的最后一个参数一定不能为NULL，否则中断申请会失败，这里传入的是ADC_DEV类型的变量*/
 ret = request_irq(IRQ_ADC, adc_irq, IRQF_SHARED, DEVICE_NAME, &adcdev);
 if (ret) 
     {
           /*错误处理*/
        printk(KERN_ERR "IRQ%d error %d\n", IRQ_ADC, ret);
        return -EINVAL;
     }

 DPRINTK( "adc opened\n");
   return 0;

}
static int adc_release(struct inode *inode, struct file *filp)
{
 DPRINTK( "adc closed\n");
 return 0;
}
static struct file_operations dev_fops = {
 owner: THIS_MODULE,
 open: adc_open,
 read: adc_read,
 release: adc_release,
};
static struct miscdevice adc_miscdev = {
 .minor = MISC_DYNAMIC_MINOR,
 .name = DEVICE_NAME,
 .fops = &dev_fops,
};
static int __init dev_init(void)
{
 int ret;
 /*  1,从平台时钟队列中获取ADC的时钟，这里为什么要取得这个时钟，因为ADC的转换频率跟时钟有关。
     系统的一些时钟定义在arch/arm/plat-s3c24xx/s3c2410-clock.c中*/
 adc_clk = clk_get(NULL, "adc");
 if (!adc_clk) {
  printk(KERN_ERR "failed to get adc clock source\n");
  return -ENOENT;
 }
 /*时钟获取后要使能后才可以使用，clk_enable定义在arch/arm/plat-s3c/clock.c中*/
 clk_enable(adc_clk);

 /*  2,将ADC的IO端口占用的这段IO空间映射到内存的虚拟地址，ioremap定义在io.h中。
      注意：IO空间要映射后才能使用，以后对虚拟地址的操作就是对IO空间的操作,
   S3C2410_PA_ADC是ADC控制器的基地址，定义在mach-s3c2410/include/mach/map.h中，0x20是虚拟地址长度大小*/
 adc_base=ioremap(S3C2410_PA_ADC,0x20);
 if (adc_base == NULL) {
  printk(KERN_ERR "Failed to remap register block\n");
  ret = -EINVAL;
      goto err_noclk;
 }

 /*   3,把看ADC注册成为misc设备，misc_register定义在miscdevice.h中
   adc_miscdev结构体定义及内部接口函数在第2步中讲,MISC_DYNAMIC_MINOR是次设备号，定义在miscdevice.h中*/
   ret = misc_register(&adc_miscdev);
   if (ret) 
     {
          /*错误处理*/
      printk(KERN_ERR "Cannot register miscdev on minor=%d (%d)\n", MISC_DYNAMIC_MINOR, ret);
      goto err_nomap;
     }
 
   printk(DEVICE_NAME "\tinitialized!\n");
 
 return 0;
 
//以下是上面错误处理的跳转点
err_noclk:
   clk_disable(adc_clk);
   clk_put(adc_clk);

err_nomap:
   iounmap(adc_base);

   return ret;

}

static void __exit dev_exit(void)
{
 
 free_irq(IRQ_ADC, &adcdev); //;释放中断
 iounmap(adc_base); /*释放虚拟地址映射空间*/
 if (adc_clk)  /*屏蔽和销毁时钟*/
 {
  clk_disable(adc_clk);
  clk_put(adc_clk);
  adc_clk = NULL;
 }
 misc_deregister(&adc_miscdev);
}
//;导出信号量“ADC_LOCK”，以便触摸屏驱动使用
EXPORT_SYMBOL(ADC_LOCK);
module_init(dev_init);
module_exit(dev_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("kaylor");
MODULE_DESCRIPTION("Mini2440 ADC Driver");
