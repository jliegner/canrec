#include "incall.h"
#include "diskio.h"
#include "ff.h"

GpioPin<PA, 5, GPIO_OUT_2MHZ, GPIO_OUT_NORMAL>       aLed1;
GpioPin<PA, 1, GPIO_OUT_2MHZ, GPIO_OUT_NORMAL>       aLed2;

// SPI2 SD-Karte
GpioPin<PB, 13, GPIO_OUT_50MHZ, GPIO_OUT_ALTNORMAL>  aSclk;
GpioPin<PB, 15, GPIO_OUT_50MHZ, GPIO_OUT_ALTNORMAL>  aMosi;
GpioPin<PB, 14, GPIO_IN, GPIO_IN_FLOAT>              aMiso;
GpioPin<PD,  2, GPIO_OUT_2MHZ, GPIO_OUT_NORMAL>      aCs(GPIO_SET);

// CAN
GpioPin<PB,  9, GPIO_OUT_50MHZ, GPIO_OUT_ALTNORMAL>  aCanTx; 
GpioPin<PB,  8, GPIO_IN, GPIO_IN_FLOAT>              aCanRx;
GpioPin<PC, 13, GPIO_OUT_2MHZ, GPIO_OUT_NORMAL>      aCanCtl(GPIO_SET);

GpioPin<PC,  9, GPIO_IN, GPIO_IN_PULLUPDOWN>         aButt(GPIO_CLR);
GpioPin<PC,  8, GPIO_IN, GPIO_IN_PULLUPDOWN>         aSilentMode(GPIO_SET);

void ClockConfig()
{
  // Resetvalue laut Datenblatt
  RCC->CR=0x81;
  
  // warten bis HSI an
  while ((RCC->CR & RCC_CR_HSIRDY)==0)  
    ;

  // externen 8 MHz Oszillator HSE einschalten 
  RCC->CR |= RCC_CR_HSEON;       

  while ((RCC->CR & RCC_CR_HSERDY)==0)  
    ;

  // Prefetchbuffer und 2 Waitstates
  FLASH->ACR=FLASH_ACR_PRFTBE | FLASH_ACR_LATENCY_2;

  RCC->CFGR=  RCC_CFGR_HPRE_DIV1    // 
            | RCC_CFGR_PPRE1_DIV2   // ABP1 36MHz
            | RCC_CFGR_PPRE2_DIV2   // ABP2 36MHz
            | RCC_CFGR_ADCPRE_DIV8  // ADC prescaler
            | RCC_CFGR_PLLMULL9     // PLL * 9
            | RCC_CFGR_PLLSRC_HSE;  // HSE fuer PLL

  // PLL einschalten
  RCC->CR |= RCC_CR_PLLON;
  while ((RCC->CR & RCC_CR_PLLRDY)==0)
    ;

  // PLL als Taktquelle
  RCC->CFGR |= RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & 0b1100) != RCC_CFGR_SWS_PLL)
    ;

  if ((RCC->BDCR & RCC_BDCR_LSERDY)==0)
    {
    // Zugriff auf die PWR Register temp. enablen
    uint32_t rcc_abp1enr=RCC->APB1ENR;
    RCC->APB1ENR|=RCC_APB1ENR_PWREN;

    // Zugriff auf das RCC->BDCR Register freigeben
    PWR->CR|=PWR_CR_DBP; 
    RCC->BDCR=RCC_BDCR_BDRST;
    RCC->BDCR=RCC_BDCR_RTCEN | RCC_BDCR_LSEON | RCC_BDCR_RTCSEL_LSE;
    PWR->CR&=~PWR_CR_DBP; 
    
    // Zugriff auf das RCC->BDCR Register wieder sperren
    PWR->CR&=~PWR_CR_DBP; 
    RCC->APB1ENR=rcc_abp1enr;
    }
}

extern "C"{ void disk_timerproc (void); }

uint32_t          uButton;
volatile uint32_t bButton;
volatile uint32_t bButtonPress=0;
volatile uint32_t TimeTick;
volatile uint32_t SecundeTick;
volatile uint32_t nSecunde=0;
volatile bool     bSekunde;
uint32_t  aLed2Timer;
extern volatile uint32_t uCanTimeTick; 
void SysTick_Handler(void)
{
  uCanTimeTick++;
  // Tastenentprellung und Flankencheck
  uButton<<=1;
  if (aButt.In())
    uButton|=1;
  if (uButton==0xffffff)
    {
    if (!bButton)
      bButtonPress=1;
    bButton=1;
    }
   if (uButton==0)
    bButton=0;
  //-----------------------------------

  if (aLed2Timer>0)
    {
    aLed2.Set();
    aLed2Timer--;
    }
  else
    aLed2.Clr();


  disk_timerproc();
  TimeTick++;
  SecundeTick++;
  if (SecundeTick>=1000)
    {
    bSekunde=1;
    SecundeTick=0;
    nSecunde++;
    }
}

FATFS Fatfs; 
DIR   aDir;
FIL   aFIL;

char caNextFileName[64];

const char *FindNextFileName()
{
  for (int n=0; n<1000; n++)
    {
    sprintf(caNextFileName, "%03i.trc", n);
    int fr=f_open(&aFIL, caNextFileName, FA_READ);
    if (fr!=FR_OK)
      {
      debug_printf("next file is %s\n", caNextFileName);
      return caNextFileName;
      }
    }
  return 0;
}

int OpenDisk()
{
  int frc=disk_initialize(0);
  if (frc!=0)
    return -1;
  frc=f_mount(&Fatfs, "/", 0);
  if (frc!=0)
    return -2;
  return 0;
}

bool bSilentMode=true;

int CanRecord()
{
  int rc=0;
  debug_printf("start. open can.\n");
  static char ca[128];
  bool bFileOpen=false;
  aCan.Init(125000, true, bSilentMode); 
  aCan.SetFilter32(1, 0x000, 0x000, CF_MASKMODE_FIFO1_STD);
  aCanCtl.Clr();

  if (OpenDisk()!=0)
    {
    debug_printf("can't open sd-card\n");
    aCan.Disable();
    return -1;
    }
  
  if (!FindNextFileName())
    {
    aCan.Disable();
    return -1;
    }
      
  uint32_t uLastSync=0;
  FRESULT fr=FR_OK;
  CanMsg aMsg;
  while (1)
    {
    if (bButtonPress)
      {
      bButtonPress=false;
      break;
      }
    if (aCan.GetNextMsg(aMsg))
      {
      aLed2Timer=50;

      // Mit der ersten CanMsg die Datei anlegen
      if (!bFileOpen)
        {
        char cafn[64];
        strcpy(cafn, caNextFileName);
        fr=f_open(&aFIL, cafn, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr)
          {
          debug_printf("fs error %i\n", fr);
          rc=-2;
          break;
          }
        f_sync(&aFIL);
        debug_printf("create file %s\n", cafn);
        bFileOpen=true;
        }

      uint32_t t=aMsg.timestamp;
      int sec=t/1000;
      int ms=t%1000;
      int l=0;
      if (aMsg.ide) 
        l=sprintf(ca, "%05i.%03i -- %08x [%x]", sec, ms, aMsg.id, aMsg.dlc);
      else
        l=sprintf(ca, "%05i.%03i --      %03x [%x]", sec, ms, aMsg.id, aMsg.dlc);
      for (int n=0; n<aMsg.dlc; n++)
        l+=sprintf(ca+l, " %02x", aMsg.data[n]);
      if (aMsg.rtr)
        l+=sprintf(ca+l, " RTR");
      sprintf(ca+l, "\n");
      UINT bw=strlen(ca);
      UINT bwr=0;
      debug_printf(ca);
      fr=f_write(&aFIL, ca, bw, &bwr); 
      if (fr)
        {
        debug_printf("fs error %i\n", fr);
        rc=-3;
        break;
        }
      }
    if (bFileOpen && bSekunde)
      {
      f_sync(&aFIL);
      bSekunde=false;
      }
    }   

  if (bFileOpen)
    f_close(&aFIL);
  debug_printf("stop. disable can.\n");
  aCan.Disable();
  return rc;
}

int main()
{
  ClockConfig();
  InitDwtDelay();
  
  for (int n=0; n<5; n++)
    {
    aLed2.Set();
    delay_ms(100);
    aLed2.Clr();
    delay_ms(100);
    }
  SysTick_Config(SystemCoreClock/SYSTICKFREQ);

  if (aSilentMode.In())
    {
    debug_printf("silent mode on\n");
    bSilentMode=true;
    }
  else
    {
    debug_printf("silent mode off\n");
    bSilentMode=false;
    }

  int rc;
  while (1)
    {
    if (bButtonPress)
      {
      bButtonPress=false;
      aLed1.Set();
      int rc=CanRecord();
      aLed1.Clr();

      // Fehler auf der SD-Karte? 
      // dann ein paar mal blinken
      if (rc!=0)
        {
        aLed1.Set();
        int cnt=10;
        while(!bButtonPress && cnt>0)
          {
          cnt--;
          delay_ms(100); 
          aLed1.Toggle();
          }
        aLed1.Clr();
        bButtonPress=false;
        }
      }
    }  
}  