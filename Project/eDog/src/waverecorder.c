/**
  ******************************************************************************
  * @file    Audio_playback_and_record/src/waverecorder.c 
  * @author  MCD Application Team
  * @version V1.0.0
  * @date    28-October-2011
  * @brief   I2S audio program 
  ******************************************************************************
  * @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2011 STMicroelectronics</center></h2>
  ******************************************************************************
  */ 

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "pdm_filter.h"
#include "waverecorder.h" 

/** @addtogroup STM32F4-Discovery_Audio_Player_Recorder
* @{
*/ 

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* SPI Configuration defines */
#define SPI_SCK_PIN                       GPIO_Pin_10
#define SPI_SCK_GPIO_PORT                 GPIOB
#define SPI_SCK_GPIO_CLK                  RCC_AHB1Periph_GPIOB
#define SPI_SCK_SOURCE                    GPIO_PinSource10
#define SPI_SCK_AF                        GPIO_AF_SPI2

#define SPI_MOSI_PIN                      GPIO_Pin_3
#define SPI_MOSI_GPIO_PORT                GPIOC
#define SPI_MOSI_GPIO_CLK                 RCC_AHB1Periph_GPIOC
#define SPI_MOSI_SOURCE                   GPIO_PinSource3
#define SPI_MOSI_AF                       GPIO_AF_SPI2

#define AUDIO_REC_SPI_IRQHANDLER          SPI2_IRQHandler

/* Audio recording frequency in Hz */
#define REC_FREQ                          16000  

/* PDM buffer input size */
#define INTERNAL_BUFF_SIZE      64

/* PCM buffer output size */
#define PCM_OUT_SIZE            16

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

/* Current state of the audio recorder interface intialization */
static uint32_t AudioRecInited = 0;
PDMFilter_InitStruct Filter;
/* Audio recording Samples format (from 8 to 16 bits) */
uint32_t AudioRecBitRes = 16; 
uint16_t RecBuf[PCM_OUT_SIZE], RecBuf1[PCM_OUT_SIZE];
/* Audio recording number of channels (1 for Mono or 2 for Stereo) */
uint32_t AudioRecChnlNbr = 1;
/* Main buffer pointer for the recorded data storing */
uint16_t* pAudioRecBuf;
/* Current size of the recorded buffer */
uint32_t AudioRecCurrSize = 0; 
/* Temporary data sample */
static uint16_t InternalBuffer[INTERNAL_BUFF_SIZE];
static uint32_t InternalBufferSize = 0;

/* Private function prototypes -----------------------------------------------*/
static void WaveRecorder_GPIO_Init(void);
static void WaveRecorder_SPI_Init(uint32_t Freq);
static void WaveRecorder_NVIC_Init(void);

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initialize wave recording
  * @param  AudioFreq: Sampling frequency
  *         BitRes: Audio recording Samples format (from 8 to 16 bits)
  *         ChnlNbr: Number of input microphone channel
  * @retval None
  */
uint32_t WaveRecorderInit(uint32_t AudioFreq, uint32_t BitRes, uint32_t ChnlNbr)
{ 
  /* Check if the interface is already initialized */
  if (AudioRecInited)
  {
    /* No need for initialization */
    return 0;
  }
  else
  {
    /* Enable CRC module */
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    
    /* Filter LP & HP Init */
    Filter.LP_HZ = 8000;
    Filter.HP_HZ = 10;
    Filter.Fs = 16000;
    Filter.Out_MicChannels = 1;
    Filter.In_MicChannels = 1;
    
    PDM_Filter_Init((PDMFilter_InitStruct *)&Filter);
    
    /* Configure the GPIOs */
    WaveRecorder_GPIO_Init();
    
    /* Configure the interrupts (for timer) */
    WaveRecorder_NVIC_Init();
    
    /* Configure the SPI */
    WaveRecorder_SPI_Init(AudioFreq);
    
    /* Set the local parameters */
    AudioRecBitRes = BitRes;
    AudioRecChnlNbr = ChnlNbr;
    
    /* Set state of the audio recorder to initialized */
    AudioRecInited = 1;
    
    /* Return 0 if all operations are OK */
    return 0;
  }  
}

/**
  * @brief  Start audio recording
  * @param  pbuf: pointer to a buffer
  *         size: Buffer size
  * @retval None
  */
uint8_t WaveRecorderStart(uint16_t* pbuf, uint32_t size)
{
/* Check if the interface has already been initialized */
  if (AudioRecInited)
  {
    /* Store the location and size of the audio buffer */
    pAudioRecBuf = pbuf;
    AudioRecCurrSize = size;
    
    /* Enable the Rx buffer not empty interrupt */
    SPI_I2S_ITConfig(SPI2, SPI_I2S_IT_RXNE, ENABLE);
    /* The Data transfer is performed in the SPI interrupt routine */
    /* Enable the SPI peripheral */
    I2S_Cmd(SPI2, ENABLE); 
   
    /* Return 0 if all operations are OK */
    return 0;
  }
  else
  {
    /* Cannot perform operation */
    return 1;
  }
}

/**
  * @brief  Stop audio recording
  * @param  None
  * @retval None
  */
uint32_t WaveRecorderStop(void)
{
  /* Check if the interface has already been initialized */
  if (AudioRecInited)
  {
    
    /* Stop conversion */
    I2S_Cmd(SPI2, DISABLE); 
    
    /* Return 0 if all operations are OK */
    return 0;
  }
  else
  {
    /* Cannot perform operation */
    return 1;
  }
}

/**
  * @brief  This function handles AUDIO_REC_SPI global interrupt request.
  * @param  None
  * @retval None
*/

void WaveRecorderCallback (int16_t *buffer, int num_samples);

void AUDIO_REC_SPI_IRQHANDLER(void)
{  
   u16 volume;
   u16 app;

  /* Check if data are available in SPI Data register */
  if (SPI_GetITStatus(SPI2, SPI_I2S_IT_RXNE) != RESET)
  {
    app = SPI_I2S_ReceiveData(SPI2);
    InternalBuffer[InternalBufferSize++] = HTONS(app);
    
    /* Check to prevent overflow condition */
    if (InternalBufferSize >= INTERNAL_BUFF_SIZE)
    {
      InternalBufferSize = 0;
     
      volume = 50;
      
      PDM_Filter_64_LSB((uint8_t *)InternalBuffer, (uint16_t *)pAudioRecBuf, volume , (PDMFilter_InitStruct *)&Filter);
      WaveRecorderCallback ((int16_t *) pAudioRecBuf, PCM_OUT_SIZE);
    }
  }
}

void WaveRecorderBeginSampling (void)
{
  WaveRecorderInit(32000,16, 1);
  WaveRecorderStart(RecBuf, PCM_OUT_SIZE);
}

/**
  * @brief  Initialize GPIO for wave recorder.
  * @param  None
  * @retval None
  */
static void WaveRecorder_GPIO_Init(void)
{  
  GPIO_InitTypeDef GPIO_InitStructure;

  /* Enable GPIO clocks */
  RCC_AHB1PeriphClockCmd(SPI_SCK_GPIO_CLK | SPI_MOSI_GPIO_CLK, ENABLE);

  /* Enable GPIO clocks */
  RCC_AHB1PeriphClockCmd(SPI_SCK_GPIO_CLK | SPI_MOSI_GPIO_CLK, ENABLE);

  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
  GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
  GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
  GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

  /* SPI SCK pin configuration */
  GPIO_InitStructure.GPIO_Pin = SPI_SCK_PIN;
  GPIO_Init(SPI_SCK_GPIO_PORT, &GPIO_InitStructure);
  
  /* Connect SPI pins to AF5 */  
  GPIO_PinAFConfig(SPI_SCK_GPIO_PORT, SPI_SCK_SOURCE, SPI_SCK_AF);
  
  /* SPI MOSI pin configuration */
  GPIO_InitStructure.GPIO_Pin =  SPI_MOSI_PIN;
  GPIO_Init(SPI_MOSI_GPIO_PORT, &GPIO_InitStructure);
  GPIO_PinAFConfig(SPI_MOSI_GPIO_PORT, SPI_MOSI_SOURCE, SPI_MOSI_AF);
}

/**
  * @brief  Initialize SPI peripheral.
  * @param  Freq :Audio frequency
  * @retval None
  */
static void WaveRecorder_SPI_Init(uint32_t Freq)
{
  I2S_InitTypeDef I2S_InitStructure;

  /* Enable the SPI clock */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2,ENABLE);
  
  /* SPI configuration */
  SPI_I2S_DeInit(SPI2);
  I2S_InitStructure.I2S_AudioFreq = 32000;
  I2S_InitStructure.I2S_Standard = I2S_Standard_LSB;
  I2S_InitStructure.I2S_DataFormat = I2S_DataFormat_16b;
  I2S_InitStructure.I2S_CPOL = I2S_CPOL_High;
  I2S_InitStructure.I2S_Mode = I2S_Mode_MasterRx;
  I2S_InitStructure.I2S_MCLKOutput = I2S_MCLKOutput_Disable;
  /* Initialize the I2S peripheral with the structure above */
  I2S_Init(SPI2, &I2S_InitStructure);

  /* Enable the Rx buffer not empty interrupt */
  SPI_I2S_ITConfig(SPI2, SPI_I2S_IT_RXNE, ENABLE);
}


/**
  * @brief  Initialize the NVIC.
  * @param  None
  * @retval None
  */
static void WaveRecorder_NVIC_Init(void)
{
  NVIC_InitTypeDef NVIC_InitStructure;

  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_3); 
  /* Configure the SPI interrupt priority */
  NVIC_InitStructure.NVIC_IRQChannel = SPI2_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}


#ifdef  USE_FULL_ASSERT

/**
* @brief  Reports the name of the source file and the source line number
*   where the assert_param error has occurred.
* @param  file: pointer to the source file name
* @param  line: assert_param error line source number
* @retval None
*/
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* User can add his own implementation to report the file name and line number,
  ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  
  /* Infinite loop */
  while (1)
  {
  }
}
#endif

/**
* @}
*/ 


/******************* (C) COPYRIGHT 2011 STMicroelectronics *****END OF FILE****/
