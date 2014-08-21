/**
  ******************************************************************************
  * @file    Audio_playback_and_record/src/waveplayer.c 
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
#include <main.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <scan.h>

/** @addtogroup STM32F4-Discovery_Audio_Player_Recorder
* @{
*/ 

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/

static uint8_t volume = 88;
extern volatile uint8_t LED_Toggle;
extern volatile int user_mode;

/* Private function prototypes -----------------------------------------------*/

// We have three buffers: two output buffers used in a ping-pong arrangement, and an input
// (microphone) circular buffer. Because the output buffers are written directly to the I2S
// interface with DMA, they must be stereo. The microphone buffer is mono and its duration
// is exactly 3 times the length of 1 of the ping-pong buffers. The idea is that during
// normal operation, the microphone buffer will vary between about 1/3 full and 2/3 full
// (with a 1/3 buffer margin on either side).

#define SAMPLE_RATE 16000       // sampling rate
#define OUT_BUFFER_SAMPLES 128  // number of samples per output ping-pong buffer
                                // /2 == stereo samples, *2 == number of bytes

#define MIC_BUFFER_SAMPLES (OUT_BUFFER_SAMPLES * 3 / 2)

static int16_t buff0 [OUT_BUFFER_SAMPLES], buff1 [OUT_BUFFER_SAMPLES], micbuff [MIC_BUFFER_SAMPLES];
static volatile uint16_t mic_head, mic_tail;    // head and tail indices to mic buffer
static volatile uint8_t next_buff;              // next output buffer to write
 
// These functions will have different instances depending on the global function selected below

static void fill_init (void);
static void fill_buffer (int16_t *buffer, int num_samples);

// define one of these three to control behavior...
// #define GENERATE_TONES      // generate pure FS tones into the output
// #define GENERATE_ECHO       // copy the microphone to the output (with some delay based on buffers)
#define GENERATE_DOGS       // BARK BARK!

// This function is called by the wav recorder (i.e. microphone sampler) when PCM samples from the
// microphone are ready. Here we store them into the circular microphone data buffer and check for
// possibly clipped values (which we use to flash the red LED as a warning). Note that we assume
// that the data will be removed from the buffer in time, so we do not check the mic_tail pointer
// for buffer overrun.

void WaveRecorderCallback (int16_t *buffer, int num_samples)
{
    static int clip_timer;
    int clip = 0, i;

    for (i = 0; i < num_samples; ++i) {
        int16_t sample = *buffer++;

        if (sample >= 32700 || sample <= -32700)
            clip = 1;

        micbuff [mic_head + i] = sample;
    }

    mic_head = (mic_head + num_samples >= MIC_BUFFER_SAMPLES) ? 0 : mic_head + num_samples;

    if (clip_timer) {
        if (!--clip_timer)
            STM_EVAL_LEDOff(LED5);
    }
    else if (clip)
        STM_EVAL_LEDOn(LED5);

    if (clip)
        clip_timer = 50;
}

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Play wave from a mass storge
  * @param  AudioFreq: Audio Sampling Frequency
  * @retval None
*/

void WavePlayBack(uint32_t AudioFreq)
{ 
  /* First, we start sampling internal microphone */
  WaveRecorderBeginSampling ();

  /* Initialize wave player (Codec, DMA, I2C) */
  WavePlayerInit(SAMPLE_RATE);
  
  /* Initialize the buffer filling function */
  fill_init ();

  /* Let the microphone data buffer get 2/3 full (which is 2 playback buffers) */
  while (mic_head < MIC_BUFFER_SAMPLES * 2 / 3);

  /* Fill the second playback buffer (the first will just be zeros to start) */
  fill_buffer (buff1, OUT_BUFFER_SAMPLES);
  
  /* Start audio playback on the first buffer (which is all zeros now) */
  Audio_MAL_Play((uint32_t)buff0, OUT_BUFFER_SAMPLES * 2);
  next_buff = 1; 

  /* LED Green Start toggling */
  LED_Toggle = LED_CTRL_GREEN_TOGGLE;
  
  /* This is the main loop of the program. We simply wait for a buffer to be exhausted
   * and then we refill it. The callback (which is triggered by DMA completion) actually
   * handles starting the next buffer playing, so we don't need to be worried about that
   * latency here. The functionality of the fill_buffer() function determines what it is
   * that we are doing (e.g., playing tones, echoing the mic, being a nervous dog, etc.)
   */

  while (1) {
    while (next_buff == 1);
    fill_buffer (buff0, OUT_BUFFER_SAMPLES);
    while (next_buff == 0);
    fill_buffer (buff1, OUT_BUFFER_SAMPLES);
  }
}

/**
  * @brief  Pause or Resume a played wave
  * @param  state: if it is equal to 0 pause Playing else resume playing
  * @retval None
  */
void WavePlayerPauseResume(uint8_t state)
{ 
  EVAL_AUDIO_PauseResume(state);   
}

/**
  * @brief  Configure the volune
  * @param  vol: volume value
  * @retval None
  */
uint8_t WaveplayerCtrlVolume(uint8_t vol)
{ 
  EVAL_AUDIO_VolumeCtl(vol);
  return 0;
}


/**
  * @brief  Stop playing wave
  * @param  None
  * @retval None
  */
void WavePlayerStop(void)
{ 
  EVAL_AUDIO_Stop(CODEC_PDWN_SW);
}
 
/**
* @brief  Initializes the wave player
* @param  AudioFreq: Audio sampling frequency
* @retval None
*/
int WavePlayerInit(uint32_t AudioFreq)
{ 
  /* Initialize I2S interface */  
  EVAL_AUDIO_SetAudioInterface(AUDIO_INTERFACE_I2S);
  
  /* Initialize the Audio codec and all related peripherals (I2S, I2C, IOExpander, IOs...) */  
  EVAL_AUDIO_Init(OUTPUT_DEVICE_AUTO, volume, AudioFreq );  
  
  return 0;
}

/**
  * @brief  MEMS accelerometre management of the timeout situation.
  * @param  None.
  * @retval None.
  */
uint32_t LIS302DL_TIMEOUT_UserCallback(void)
{
  /* MEMS Accelerometer Timeout error occured */
  while (1)
  {   
  }
}

/*--------------------------------
Callbacks implementation:
the callbacks prototypes are defined in the stm324xg_eval_audio_codec.h file
and their implementation should be done in the user code if they are needed.
Below some examples of callback implementations.
--------------------------------------------------------*/
/**
* @brief  Calculates the remaining file size and new position of the pointer.
* @param  None
* @retval None
*/
void EVAL_AUDIO_TransferComplete_CallBack(uint32_t pBuffer, uint32_t Size)
{
  /* Called when the previous DMA playback buffer is completed. Here we simply
   * start playing the other buffer and signal the main loop that it can refill
   * the buffer we just played.
   */
 
  if (next_buff == 0) {
    Audio_MAL_Play((uint32_t)buff0, OUT_BUFFER_SAMPLES * 2);
    next_buff = 1; 
  }
  else {
    Audio_MAL_Play((uint32_t)buff1, OUT_BUFFER_SAMPLES * 2);
    next_buff = 0; 
  }

}

/**
* @brief  Manages the DMA Half Transfer complete interrupt.
* @param  None
* @retval None
*/
void EVAL_AUDIO_HalfTransfer_CallBack(uint32_t pBuffer, uint32_t Size)
{  
#ifdef AUDIO_MAL_MODE_CIRCULAR
    
#endif /* AUDIO_MAL_MODE_CIRCULAR */
  
  /* Generally this interrupt routine is used to load the buffer when 
  a streaming scheme is used: When first Half buffer is already transferred load 
  the new data to the first half of buffer while DMA is transferring data from 
  the second half. And when Transfer complete occurs, load the second half of 
  the buffer while the DMA is transferring from the first half ... */
  /* 
  ...........
  */
}

/**
* @brief  Manages the DMA FIFO error interrupt.
* @param  None
* @retval None
*/
void EVAL_AUDIO_Error_CallBack(void* pData)
{
  /* Stop the program with an infinite loop */
  while (1)
  {}
  
  /* could also generate a system reset to recover from the error */
  /* .... */
}

/**
* @brief  Get next data sample callback
* @param  None
* @retval Next data sample to be sent
*/
uint16_t EVAL_AUDIO_GetSampleCallBack(void)
{
  return 0;
}


#ifndef USE_DEFAULT_TIMEOUT_CALLBACK
/**
  * @brief  Basic management of the timeout situation.
  * @param  None.
  * @retval None.
  */
uint32_t Codec_TIMEOUT_UserCallback(void)
{   
  return (0);
}
#endif /* USE_DEFAULT_TIMEOUT_CALLBACK */
/*----------------------------------------------------------------------------*/

/* This version of the fill functions just plays a couple of sine waves (one
 * in each channel). This was created to verify clean playback and to
 * investigate the use of float data native to the STM32F4. Obviously in this
 * version the microphone data is discarded.
 */

#ifdef GENERATE_TONES

#define M_TWOPI (3.14159265358979323F * 2.0F)

static float angle_left, velocity_left;
static float angle_right, velocity_right;

static void fill_init (void)
{
  velocity_left = M_TWOPI * 60.0F / (float) SAMPLE_RATE;
  velocity_right = M_TWOPI * 500.0F / (float) SAMPLE_RATE;
}

static void fill_buffer (int16_t *buffer, int num_samples)
{
    int count = num_samples / 2;

    while (count--) {
        *buffer++ = floorf (sinf (angle_left) * 10000.0F);
        if ((angle_left += velocity_left) > M_TWOPI)
            angle_left -= M_TWOPI;

        *buffer++ = floorf (sinf (angle_right) * 10000.0F);
        if ((angle_right += velocity_right) > M_TWOPI)
            angle_right -= M_TWOPI;
    }
}

#endif

/*----------------------------------------------------------------------------*/

/* This version of the fill functions just echoes the microphone output back
 * to the headphones. This was created to verify clean microphone sampling
 * (ST's recording demo was full of gaps while the mass storage was writing)
 * and to make sure we could really do full-duplex audio (which I needed for
 * another project). Note that you can get a pretty long delay by increasing
 * the size of the buffers (and these don't count toward the 32K max). There
 * are some delay values that, when played back in headphones, make it almost
 * impossible to talk. Big fun!
 */

#ifdef GENERATE_ECHO

static void fill_init (void)
{
}

static void fill_buffer (int16_t *buffer, int num_samples)
{
    int count = num_samples / 2;

    while (count--) {
        *buffer++ = micbuff [mic_tail];
        *buffer++ = micbuff [mic_tail];
        mic_tail = (mic_tail + 1 >= MIC_BUFFER_SAMPLES) ? 0 : mic_tail + 1;
    }
}

#endif

/*----------------------------------------------------------------------------*/

/* This version of the fill functions send the microphone audio data to the
 * scan_audio() function to detect "knocks" or "rings", and if detected plays
 * samples of a great big mean barking dog. To avoid obviously repeating the
 * same bark sequence, about 30 seconds worth is stored in flash and six
 * individual segments are stored in an array. We don't really need for all
 * this to be full-duplex because we can't detect anything while the dog is
 * barking anyway, but is design is cleaner this way.
 */

#ifdef GENERATE_DOGS

#define CANNED_AUDIO_START 0x08010000       // raw 16-bit PCM mono audio data is here

/* The canned subclips are defined here as a sample offset from the beginning of the
 * data. plus a sample count. The clips start right at the onset of a bark, so that
 * we have the minimum delay from the detection. In the first segment, the dog sounds
 * a little surprised, so we rewind to that one if 60 seconds go by with nothing
 * happening.
 */

static struct clip {
    int start_sample, num_samples;
} canned_clips [] = {
    3840, 78080,
    81920, 94080,
    176000, 78400,
    254400, 52640,
    307040, 99360,
    406400, 48000,
    0, 0
}, *canned_ptr = canned_clips;

static int16_t *canned_audio;
static int canned_samples, samples_since_trigger;

static void fill_init (void)
{
    scan_audio_init ();     // the scanner needs to be initialized
}

// Fill the specified buffer with the specified number of samples. Since this is stereo
// data that we are writing (for now), we half the sample count and duplicate every
// sample when we write. The user_mode (which is incremented by the user button) selects
// high detection sensitivity (bit 1) and whether we play a single bark for debug and
// setup purposes (bit 0).

static void fill_buffer (int16_t *buffer, int num_samples)
{
    int count = num_samples / 2, detection = 0;

    // First, send the microphone data to the audio scanner to look for knocks and rings.
    // Because the microphone sampling and the audio playback are running at the same
    // sample rate, we know that we have the desired number of samples so we don't have
    // to check the mic_head pointer, but we do have to check for wrapping, obviously.

    while (count) {
        int samples_to_scan = count;

        if (mic_tail + samples_to_scan > MIC_BUFFER_SAMPLES)
            samples_to_scan = MIC_BUFFER_SAMPLES - mic_tail;

        detection |= scan_audio (micbuff + mic_tail, samples_to_scan, NULL,
            ((user_mode & 2) ? SCAN_HIGH_SENSITIVITY : 0) | SCAN_DISP_THRESHOLDS | SCAN_DISP_EVENTS | SCAN_DISP_PEAKS);

        mic_tail = (mic_tail + samples_to_scan >= MIC_BUFFER_SAMPLES) ? 0 : mic_tail + samples_to_scan;
        count -= samples_to_scan;
    }

    // If we detected a knock or a ring (and we are not already playing canned audio for
    // a previous trigger) then we start playing canned audio here. This also switches the
    // toggling LED from the green to the orange.

    if (detection && !canned_samples) {
        if (user_mode & 1) {
            canned_audio = (int16_t *) CANNED_AUDIO_START + 68464;
            canned_samples = 8000;
        }
        else {
            canned_audio = (int16_t *) CANNED_AUDIO_START + canned_ptr->start_sample;
            canned_samples = canned_ptr->num_samples;
            if (!(++canned_ptr)->num_samples)
                canned_ptr = canned_clips;
        }

        /* LED toggling green --> orange */
        LED_Toggle &= ~LED_CTRL_GREEN_TOGGLE;
        LED_Toggle |= LED_CTRL_ORANGE_TOGGLE | LED_CTRL_GREEN_OFF;
        samples_since_trigger = 0;
    }

    // Finally, we fill the audio buffer either with canned audio (if we detected something)
    // or silence (if all's quiet).

    count = num_samples / 2;

    while (count--)
        if (canned_samples) {
            *buffer++ = *canned_audio;
            *buffer++ = *canned_audio++;
            if (!--canned_samples) {
                /* LED toggling orange --> green */
                LED_Toggle &= ~LED_CTRL_ORANGE_TOGGLE;
                LED_Toggle |= LED_CTRL_GREEN_TOGGLE | LED_CTRL_ORANGE_OFF;
            }
        }
        else {
            *buffer++ = 0;
            *buffer++ = 0;
        }

    // If it's been over a minute since our last trigger, reset the canned sequence because the
    // first one sounds more surprised.

    if (!canned_samples && !detection && (samples_since_trigger += num_samples / 2) > 16000 * 60)
        canned_ptr = canned_clips;
}

#endif

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
