// rest of GPIOs have been chose arbitrarily, with the only constraint of not using
// GPIO 14 (TXD)/GPIO 15 (RXD)/06 (GND) on P1. instead I use GND on P2 header, pin 8
// IMPORTANT: BE VERY CAREFUL TO CONNECT VCC TO P1-01 (3.3V) AND *NOT* P1-02 (5V) !!
//
// Original code
//    https://www.raspberrypi.org/forums/viewtopic.php?f=44&t=16775
// gcc -O3 iphone_nand.c -o iphone_nand
//

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>

//#define DEBUG 1

#define PAGE_SIZE               2048  //17664//8936//16448    // 2112
#define SPARE_SIZE              0//64
#define MAX_WAIT_READ_BUSY      1000

#define BCM2708_PERI_BASE       0x3F000000  // ****************For Raspberry pi 2
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000)

#define N_READ_BUSY             16 // pulled up by RPi, this is also useful
#define N_WRITE_ENABLE           6
#define N_READ_ENABLE            5
#define ADDRESS_LATCH_ENABLE    21
#define COMMAND_LATCH_ENABLE    20
#define N_CHIP_ENABLE_0                 19      //CE0
#define N_CHIP_ENABLE_1                 13      //CE1
int data_to_gpio_map[8] = { 25, 24, 23, 18, 22, 27, 17, 4  }; // 25 is NAND IO 0, etc.

volatile unsigned int *gpio;


inline void INP_GPIO(int g)
{
#ifdef DEBUG
        printf("setting direction of GPIO#%02d to input\n", g);
#endif
        (*(gpio+((g)/10)) &= ~(7<<(((g)%10)*3)));
}

inline void OUT_GPIO(int g)
{
        INP_GPIO(g);
#ifdef DEBUG
        printf("setting direction of GPIO#%02d to output\n", g);
#endif
        *(gpio+((g)/10)) |= (1<<(((g)%10)*3));
}


inline void GPIO_SET_1(int g)
{
#ifdef DEBUG
        printf("setting   GPIO#%02d to 1\n", g);
#endif
        *(gpio +  7)  = 1 << g;
}

inline void GPIO_SET_0(int g)
{
#ifdef DEBUG
        printf("setting   GPIO#%02d to 0\n", g);
#endif
        *(gpio + 10)  = 1 << g;
}

inline int GPIO_READ(int g)
{
        int x = (*(gpio + 13) & (1 << g)) >> g;
#ifdef DEBUG
        printf("GPIO#%02d     reads as %d\n", g, x);
#endif
        return x;
}

inline void set_data_direction_in(void)
{
        int i;
#ifdef DEBUG
        printf("data direction => IN\n");
#endif
        for (i = 0; i < 8; i++)
                INP_GPIO(data_to_gpio_map[i]);
}

inline void set_data_direction_out(void)
{
        int i;
#ifdef DEBUG
        printf("data direction => OUT\n");
#endif
        for (i = 0; i < 8; i++)
                OUT_GPIO(data_to_gpio_map[i]);
}

inline int GPIO_DATA8_IN(void)
{
        int i, data;
        for (i = data = 0; i < 8; i++, data = data << 1) {
                data |= GPIO_READ(data_to_gpio_map[7 - i]);
        }
        data >>= 1;
#ifdef DEBUG
        printf("GPIO_DATA8_IN: data=%02x\n", data);
#endif
        return data;
}

inline void GPIO_DATA8_OUT(int data)
{
        int i;
#ifdef DEBUG
        printf("GPIO_DATA8_OUT: data=%02x\n", data);
#endif
        for (i = 0; i < 8; i++, data >>= 1) {
                if (data & 1)
                        GPIO_SET_1(data_to_gpio_map[i]);
                else
                        GPIO_SET_0(data_to_gpio_map[i]);
        }
}

void SEND_CMD(int cmd)
{
        GPIO_SET_1(COMMAND_LATCH_ENABLE);
        set_data_direction_out(); GPIO_DATA8_OUT(cmd);
        GPIO_SET_0(N_WRITE_ENABLE);
        GPIO_SET_1(N_WRITE_ENABLE);
        GPIO_SET_0(COMMAND_LATCH_ENABLE);
}

void SEND_ADDR(int addr)
{
        GPIO_SET_1(ADDRESS_LATCH_ENABLE);
        set_data_direction_out(); GPIO_DATA8_OUT(addr); // Read ID byte 2
        GPIO_SET_0(N_WRITE_ENABLE);
        GPIO_SET_1(N_WRITE_ENABLE);
        GPIO_SET_0(ADDRESS_LATCH_ENABLE);
}

void GET_STATUS()
{       //unsigned char status;
        uint8_t status;
        while(1){
        GPIO_SET_1(COMMAND_LATCH_ENABLE);
        set_data_direction_out(); GPIO_DATA8_OUT(0x70);
        GPIO_SET_0(N_WRITE_ENABLE);
        GPIO_SET_1(N_WRITE_ENABLE);
        GPIO_SET_0(COMMAND_LATCH_ENABLE);
        set_data_direction_in();
        GPIO_SET_0(N_READ_ENABLE);
        status=GPIO_DATA8_IN();
        GPIO_SET_1(N_READ_ENABLE);

        shortpause();
        printf("STATUS(DQ0=0:pass,DQ6=0:busy):%x\n",status);
        if (((status>>6) & 1)==1) break;
        }

}

void GET_FEATURE()
{
        GPIO_SET_1(COMMAND_LATCH_ENABLE);
        set_data_direction_out(); GPIO_DATA8_OUT(0xEE);
        GPIO_SET_0(N_WRITE_ENABLE);
        GPIO_SET_1(N_WRITE_ENABLE);
        GPIO_SET_0(COMMAND_LATCH_ENABLE);
                GPIO_SET_1(ADDRESS_LATCH_ENABLE);
                GPIO_DATA8_OUT(0x80);
                GPIO_SET_0(N_WRITE_ENABLE);
        GPIO_SET_1(N_WRITE_ENABLE);
                GPIO_SET_0(ADDRESS_LATCH_ENABLE);
        set_data_direction_in();
        GPIO_SET_0(N_READ_ENABLE);
        printf("Interface Setting:(1:SDR,0:Toggle):%x\n",GPIO_DATA8_IN());
        GPIO_SET_1(N_READ_ENABLE);

}

int delay = 300;  // 10,50,100->read error, 500,300 ok -> 200KHz~300Khz
int shortpause()
{
        int i;
        volatile static int dontcare = 0;
        for (i = 0; i < delay; i++) {
                dontcare++;
        }
}


int main(int argc, char **argv)
{

        int mem_fd;

        if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0) {
                perror("open /dev/mem, are you root?");
                return -1;
        }
        if ((gpio = (volatile unsigned int *) mmap((caddr_t) 0x13370000, 4096, PROT_READ|PROT_WRITE,
                                                MAP_SHARED|MAP_FIXED, mem_fd, GPIO_BASE)) == MAP_FAILED) {
                perror("mmap GPIO_BASE");
                close(mem_fd);
                return -1;
        }

        if (argc < 2){
usage:
                GPIO_SET_1(N_CHIP_ENABLE_0);
                printf("usage: %s <command> ...\n" \
                        "\tthis program assumes PAGE_SIZE == %d (this can be changed at the top of the source)\n" \
                        "\tread_id (no arguments) : read the 5-byte device ID\n" \
                        "\tread_page <page number> <# of pages> <output filename> : read N pages including spare\n" \
                        "\tsend_command" \
                        "\twear_test <Block # (0-1060)> <test cycle>\n" ,
                        //"\tread_data <page number> <# of pages> <output filename> : read N pages, discard spare\n\n",
                        argv[0], PAGE_SIZE);
                close(mem_fd);
                return -1;
        }


        if (strcmp(argv[1], "read_id") == 0) {
                return read_id(NULL);
        }

        if (strcmp(argv[1], "read_page") == 0) {
                if (argc != 5) goto usage;
                if (atoi(argv[3]) <= 0) {
                        printf("# of pages must be > 0\n");
                        return -1;
                }
                return read_pages(atoi(argv[2]), atoi(argv[3]), argv[4], 1);
        }
        if (strcmp(argv[1], "wear_test") == 0) {
                if (argc != 4) goto usage;
                if (atoi(argv[3])<= 0) {
                        printf("wear cycle counts must be >0 \n");
                        return -1;
                        }
                wear_test(atoi(argv[2]),atoi(argv[3]));
                return 0;
        }
        if (strcmp(argv[1], "send_command") == 0){
                return send_command();
        }

        printf("unknown command '%s'\n", argv[2]);
        goto usage;

        return 0;
}

void error_msg(char *msg)
{
        printf("%s\nbe sure to check wiring, and check that pressure is applied on both sides of 360 Clip\n" \
                "sometimes it is required to move slightly the 360 Clip in case of a false contact\n", msg);
}

int read_id(unsigned char id[8])
{
        int i;
        unsigned char buf[8];
        unsigned char jbuf[104];
        // Power Up to NAND 3V0, 1V8
       // usleep(50000);


        // init value
        OUT_GPIO(N_CHIP_ENABLE_0);              GPIO_SET_1(N_CHIP_ENABLE_0);
        OUT_GPIO(N_CHIP_ENABLE_1);              GPIO_SET_1(N_CHIP_ENABLE_1);
        OUT_GPIO(COMMAND_LATCH_ENABLE);         GPIO_SET_0(COMMAND_LATCH_ENABLE);
        OUT_GPIO(ADDRESS_LATCH_ENABLE);         GPIO_SET_0(ADDRESS_LATCH_ENABLE);
        OUT_GPIO(N_WRITE_ENABLE);               GPIO_SET_1(N_WRITE_ENABLE);
        OUT_GPIO(N_READ_ENABLE);                GPIO_SET_1(N_READ_ENABLE);
        INP_GPIO(N_READ_BUSY);

        set_data_direction_out();
        // read id start
        // reset NAND assert CE0, CLE, set FF
        GPIO_SET_0(N_CHIP_ENABLE_0);
        printf("Device reset (0xFF)\n");
        SEND_CMD(0xFF);
        printf("Checking device ready/busy...\n");


        while(1){
                if (GPIO_READ(N_READ_BUSY)){
                        printf(" Device ready\n");
                        break;
                        }
                }

        GET_FEATURE();
//============ID READ (90h 00h)========================
        SEND_CMD(0x90);
        SEND_ADDR(0x00);

        set_data_direction_in();
        for (i = 0; i < 8; i++) {
                GPIO_SET_0(N_READ_ENABLE);
     //           usleep(0.02);                          // Tar=10ns
                buf[i] = GPIO_DATA8_IN();
                GPIO_SET_1(N_READ_ENABLE);
      //          usleep(0.05);                 //Trea=20ns
        }
        if (id != NULL)
                memcpy(id, buf, 8);
        else {
                printf("ID (8 cycles):");
                for (i = 0; i < 8; i++)
                        printf("%02x ", buf[i]);
                printf("\n");
        }
//=============JEDEC ID READ (90h 40h)===================
        SEND_CMD(0x90);
        SEND_ADDR(0x40);

        set_data_direction_in();
        for (i = 0; i < 104; i++) {
                GPIO_SET_0(N_READ_ENABLE);
               // usleep(0.02);
                jbuf[i] = GPIO_DATA8_IN();
                GPIO_SET_1(N_READ_ENABLE);
              //  usleep(0.05);
        }
        if (id != NULL)
                memcpy(id, jbuf, 104);
        else {
                printf("JEDEC ID (104 cycles):");
                for (i = 0; i < 104; i++)
                        printf("%02x ", jbuf[i]);
                 printf("\n");
        }


        return 0;
}

inline int page_to_address(int page, int address_byte_index)
{
        switch(address_byte_index) {
        case 3:
                return page & 0xff;
        case 4:
                return (page >>  8) & 0xff;
        case 5:
                return (page >> 16) & 0xff;
        default:
                return 0;
        }
}


int send_command(){
        int *carray, *darray, *rarray;
        int i, cycles, vtemp, cmdtemp, rcount;
        char res[2];

                // init value
        OUT_GPIO(N_CHIP_ENABLE_0);              GPIO_SET_1(N_CHIP_ENABLE_0);
        OUT_GPIO(N_CHIP_ENABLE_1);              GPIO_SET_1(N_CHIP_ENABLE_1);
        OUT_GPIO(COMMAND_LATCH_ENABLE);         GPIO_SET_0(COMMAND_LATCH_ENABLE);
        OUT_GPIO(ADDRESS_LATCH_ENABLE);         GPIO_SET_0(ADDRESS_LATCH_ENABLE);
        OUT_GPIO(N_WRITE_ENABLE);               GPIO_SET_1(N_WRITE_ENABLE);
        OUT_GPIO(N_READ_ENABLE);                GPIO_SET_1(N_READ_ENABLE);
        INP_GPIO(N_READ_BUSY);

        printf("===This option lets you send a crafted command to a NAND flash memory===\n");
        printf("[Step 1]Please specify the number of cycles required for your command.\n For example, JEDEC Read ID command (CMD90h, ADDR40h) needs 2 cycles:");
        scanf("%d",&cycles);
             if (cycles<=0){
                        printf("Cycle count needs to be mode than 0\n");
                        return 0;
                           }

        carray = (int *)malloc(cycles * sizeof (int));
        if(carray == NULL) {
           printf("unable to allocate memory\n");
                 exit(1);
                }
        darray = (int *)malloc(cycles * sizeof (int));
        if (darray == NULL){
                printf ("unable to allocate memory\n");
                exit(1);
                }
        for(i=0; i<cycles; i++){
         printf("[Step2-%d/%d] Specify the type of %dth cycle. Type 1, 2, or 3 [1 Command, 2: Address, 3: Data]:",i+1, cycles, i+1);

                scanf("%d",&cmdtemp);
                if (cmdtemp!=1 && cmdtemp!=2 && cmdtemp!=3){
                        printf("Type only 1, 2 or 3\n");
                        return 0;
                        }
                carray[i]=cmdtemp;
                printf("Type the value of the %dth Command/Address/Data in *Hex*:",i+1);
                scanf("%x",&vtemp);
                darray[i]=vtemp;
                }

        printf("[Step3] Speficy the number of bytes to be read.  Type 0 if no read is required:");
        scanf("%d",&rcount);
        rarray = (int *)malloc(rcount * sizeof (int));

        for (i=0; i<cycles; i++){
                switch (carray[i]){
                        case 1:
                                printf("Cycle%d: CMD 0x%x \n", i+1, darray[i]);
                                break;
                        case 2:
                                printf("Cycle%d: Addr 0x%x \n", i+1, darray[i]);
                                break;
                        case 3:
                                printf("Cycle%d: Data 0x%x \n", i+1, darray[i]);
                                break;
                                }
                        }
        printf("Read data cycle: %d\n", rcount);
        printf ("Please type [Y]/[y] to send the command set shown above.  Type [N]/[n] to cancel.\n");
        scanf("%s",res);

        if (strcmp(res,"Y")==0 || strcmp(res,"y")==0 ){
                set_data_direction_out();
                printf("Resetting device\n");
                SEND_CMD(0xFF);
                printf("Checking device ready/busy...\n");
                usleep(0.1);
                   while(1){
                        if (GPIO_READ(N_READ_BUSY)){
                         printf(" Device ready\n");
                         break;
                                }
                }

                for  (i=0; i<cycles; i++){
                switch (carray[i]){
                        case 1:
                                printf("Sending command %x\n",darray[i]);
                                SEND_CMD(darray[i]);
                                break;
                        case 2:
                                printf("Sending address %x\n",darray[i]);
                                SEND_ADDR(darray[i]);
                                break;
                        case 3:
                                printf("Sending data %x\n", darray[i]);
                                GPIO_SET_0(N_WRITE_ENABLE);
                                GPIO_DATA8_OUT(darray[i]);
                                GPIO_SET_1(N_WRITE_ENABLE);
                                break;
                        }
                }
                usleep(0.1);
                while(1){
                if (GPIO_READ(N_READ_BUSY)){
                    //    printf(" Device ready\n");
                        break;
                        }
                }
        set_data_direction_in();

           for  (i=0; i<rcount; i++){
                        GPIO_SET_0(N_READ_ENABLE);
                        shortpause();
                        rarray[i]  = GPIO_DATA8_IN();
                        GPIO_SET_1(N_READ_ENABLE);
                }
        printf("Read data:");
          for (i=0; i<rcount; i++){
                printf ("%x ",rarray[i]);
                }
        printf("\n");

        }
        else{ printf("Cancelled\n");
                return 0;
        }

        free(carray);
        free(darray);
        }



int wear_test(blkaddr, cycle){
        unsigned char databuf[256];
        int pageaddr;
        int i,j;
        uint8_t reg;
        uint8_t bit;
        uint8_t tap;
        uint8_t offset = 4;
        uint8_t len = 3;
        uint8_t mask = (~(0xFF << len)) << offset;

        // init value
        OUT_GPIO(N_CHIP_ENABLE_0);              GPIO_SET_1(N_CHIP_ENABLE_0);
        OUT_GPIO(N_CHIP_ENABLE_1);              GPIO_SET_1(N_CHIP_ENABLE_1);
        OUT_GPIO(COMMAND_LATCH_ENABLE);         GPIO_SET_0(COMMAND_LATCH_ENABLE);
        OUT_GPIO(ADDRESS_LATCH_ENABLE);         GPIO_SET_0(ADDRESS_LATCH_ENABLE);
        OUT_GPIO(N_WRITE_ENABLE);               GPIO_SET_1(N_WRITE_ENABLE);
        OUT_GPIO(N_READ_ENABLE);                GPIO_SET_1(N_READ_ENABLE);
        INP_GPIO(N_READ_BUSY);

        set_data_direction_out();
        GPIO_SET_0(N_CHIP_ENABLE_0);
        printf("Device reset (0xFF)\n");
        SEND_CMD(0xFF);
        printf("Checking device ready/busy...\n");
        usleep(0.1);
         while(1){
                if (GPIO_READ(N_READ_BUSY)){
                        printf(" Device ready\n");
                        break;
                        }
                }

        for (j=0; j<cycle; j++){
        printf("======Cycle %d\n=========",j);
        printf("Erasing block %x\n",blkaddr);
//      SEND_CMD(0xDF);
        SEND_CMD(0x60);
        SEND_ADDR(0x00);
        SEND_ADDR(blkaddr & 0xff);
        SEND_ADDR(blkaddr>>8 & 0xff);
        SEND_CMD(0xD0);
        usleep(0.1);
        while(1){
                if (GPIO_READ(N_READ_BUSY)){
                        printf(" Device ready\n");
                        break;
                        }
                }
        GET_STATUS();
        usleep(0.1);
        for (pageaddr=0; pageaddr<256; pageaddr++){
                set_data_direction_out();
                printf("writing page:%x",pageaddr);
        //      SEND_CMD(0xDA);
                SEND_CMD(0x80);
                SEND_ADDR(0x00);
                SEND_ADDR(0x00);
                SEND_ADDR(pageaddr);
                SEND_ADDR(blkaddr & 0xff);
                SEND_ADDR(blkaddr>>8 & 0xff);

                reg=pageaddr;
                ////making random data(LFSR) this is here since I don't know how to return a buffer from a function

                 for (i=0; i<256; i++){
                        databuf[i]=reg;
                        //printf ("%x ",databuf[i]);
                        if ((reg & 0x7F) == 0)  tap = ((reg&0x80) >> 7) ^ 1;
                        else tap = (( reg&0x80 ) >> 7) ^ 0;
                        bit=((reg & 0x08)^(tap << 3))|((reg & 0x10)^(tap << 4))|((reg & 0x20)^(tap << 5));
                        reg=((reg << 1) & ~mask)|((bit<<1)& mask)| tap;
                        }
                usleep(0.1);

                set_data_direction_out();
                for (i=0; i<PAGE_SIZE; i++){
                        GPIO_SET_0(N_WRITE_ENABLE);
                        GPIO_DATA8_OUT(databuf[i%256]);
                        GPIO_SET_1(N_WRITE_ENABLE);
                }
                usleep(0.05);
                SEND_CMD(0x10);
                usleep(0.1);
                while(1){
                if (GPIO_READ(N_READ_BUSY)){
                        printf(" Device ready\n");
                        break;
                        }
                usleep(0.1);
                }
        GET_STATUS();
//      printf("%d\n",pageaddr);

        }//block per page (256 times)
        } //cycle count
        GPIO_SET_1(N_CHIP_ENABLE_0);
}



int read_pages(int first_page_number, int number_of_pages, char *outfile, int write_spare)
{
        int page, i, n, retry_count;
        unsigned char id[5], id2[5];
        unsigned char buf[PAGE_SIZE+SPARE_SIZE];
        FILE *f = fopen(outfile, "w+");

        if (f == NULL) {
                perror("fopen output file");
                return -1;
        }

        // Power Up to NAND 3V0, 1V8

        // init value
        OUT_GPIO(N_CHIP_ENABLE_0);              GPIO_SET_1(N_CHIP_ENABLE_0);
        OUT_GPIO(N_CHIP_ENABLE_1);              GPIO_SET_1(N_CHIP_ENABLE_1);
        OUT_GPIO(COMMAND_LATCH_ENABLE);         GPIO_SET_0(COMMAND_LATCH_ENABLE);
        OUT_GPIO(ADDRESS_LATCH_ENABLE);         GPIO_SET_0(ADDRESS_LATCH_ENABLE);
        OUT_GPIO(N_WRITE_ENABLE);               GPIO_SET_1(N_WRITE_ENABLE);
        OUT_GPIO(N_READ_ENABLE);                GPIO_SET_1(N_READ_ENABLE);
        INP_GPIO(N_READ_BUSY);
        set_data_direction_out();

        // read start
        // Reset
        GPIO_SET_0(N_CHIP_ENABLE_0);
        GPIO_SET_1(COMMAND_LATCH_ENABLE);
        GPIO_SET_1(N_WRITE_ENABLE);
        GPIO_DATA8_OUT(0xFF);
        GPIO_SET_0(N_WRITE_ENABLE);     // reset
        usleep(500);
        GPIO_SET_1(N_WRITE_ENABLE); //
        usleep(50000);

        for (page = first_page_number;   page < (first_page_number + number_of_pages)  ; page++) {
        //      SEND_CMD(0xDA);
                SEND_CMD(0x00);
                SEND_ADDR(0x00);
                SEND_ADDR(0x00);
                SEND_ADDR(page);
                SEND_ADDR((page >> 8) & 0xFF);
                SEND_ADDR((page >> 16) & 0xFF);
                SEND_CMD(0x30);

/*----Busy 時間計測-------------------------------------------------*/
// http://8ttyan.hatenablog.com/entry/2015/02/03/003428　　を参考にしました。
/*----１------------------------------------------------------------*/
        struct timeval myTime1;         // time_t構造体を定義．1970年1月1日からの秒数を格納するもの
        struct timeval myTime2;         // time_t構造体を定義．1970年1月1日からの秒数を格納するもの
        struct tm *time_st;             // tm構造体を定義．年月日時分秒をメンバ変数に持つ構造体
//        const char weekName[7][4] = {   // 曜日は数字としてしか得られないので，文字列として用意
//            "Sun",
//            "Mon",
//            "Tue",
//            "Wed",
//            "Thu",
//            "Fri",
//            "Sat"
//        };
    /* 時刻取得1 */
    gettimeofday(&myTime1, NULL);        // 現在時刻を取得してmyTimeに格納．通常のtime_t構造体とsuseconds_tに値が代入される
/*----------------------------------------------------------------*/

                while(1){
                if (GPIO_READ(N_READ_BUSY)){
                        printf(" Device ready ");  // 改行を削除してます
                        break;
                        }
                }

/*----２------------------------------------------------------------*/
    /* 時刻取得２ */
    gettimeofday(&myTime2, NULL);            // 現在時刻を取得してmyTimeに格納．通常のtime_t構造体とsuseconds_tに値が代入される
    time_st = localtime(&myTime1.tv_sec);    // time_t構造体を現地時間でのtm構造体に変換
    time_st = localtime(&myTime2.tv_sec);    // time_t構造体を現地時間でのtm構造体に変換
/*----------------------------------------------------------------*/

                set_data_direction_in();
                usleep(1);
                for (i = 0; i < PAGE_SIZE+SPARE_SIZE ; i++) {
                        GPIO_SET_0(N_READ_ENABLE);
                        shortpause();
                        buf[i]  = GPIO_DATA8_IN();      // ********
                        GPIO_SET_1(N_READ_ENABLE);
//                      usleep(1);
                }
                printf("page %08d(dec) %08x(hex) ", page, page);   // 改行を削除してます

/*----３------------------------------------------------------------*/
    			printf(": Busy time = %06d micro sec\n" , myTime2.tv_usec - myTime1.tv_usec);  //差分をprint
/*----------------------------------------------------------------*/

                if (fwrite(buf, PAGE_SIZE+SPARE_SIZE, 1, f) != 1) {
                        perror("fwrite");
                        return -1;
                }
        }
}
