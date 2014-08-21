////////////////////////////////////////////////////////////////////////////
//                             **** eDog ****                             //
//                                                                        //
//                  Electronic Dog Home Security System                   //
//                                 on the                                 //
//                           STM32F4-Discovery                            //
//                                                                        //
//                    Copyright (c) 2014 David Bryant                     //
//                          All Rights Reserved                           //
//        Distributed under the GNU Software License (see COPYING)        //
////////////////////////////////////////////////////////////////////////////

// scan.c
//
// David Bryant
// August 13, 2014

// This module provides the functionality of scanning a 16 Khz mono audio stream and identifying possible
// instances of intentional "knocking" and doorbell "ringing". The detection algorithms use in common a
// transient detector which is implemented entirely in the time domain. A "knock" is detected as three
// transients spaced almost equally in time and in the range of normal knocking (about 2.66 to 8 Hz). The
// "ring" detector uses an additional narrow bandpass biquad filter tuned to the fundamental frequency of
// the bell to determine if a detected transient is actually the bell. 

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "scan.h"

// Local macros. Some are configurable to change the characteristics of the detection.

#define SAMPLING_RATE 16000

#define MAX_NUM_PEAKS 16
#define KNOCK_MAX_SPAN 12000
#define KNOCK_MIN_SPAN 4000

#define WINDOW_BITS 8
#define WINDOW_SIZE (1 << WINDOW_BITS)
#define WINDOW_MASK (WINDOW_SIZE - 1)

#define NORMALIZATION_LEVEL 128
#define ANALYSIS_INTERVAL (SAMPLING_RATE/10)

#define HIGH_KNOCK_MAX_RATIO 1.2F
#define LOW_KNOCK_MAX_RATIO 1.1F
#define KNOCK_MAX_RATIO (flags & SCAN_HIGH_SENSITIVITY ? HIGH_KNOCK_MAX_RATIO : LOW_KNOCK_MAX_RATIO)

#define HIGH_THRESHOLD_SCALING 1.25F
#define LOW_THRESHOLD_SCALING 1.5F
#define THRESHOLD_SCALING (flags & SCAN_HIGH_SENSITIVITY ? HIGH_THRESHOLD_SCALING : LOW_THRESHOLD_SCALING)

#define HIGH_SPURIOUS_REJECTION_RATIO 0.75F
#define LOW_SPURIOUS_REJECTION_RATIO 0.5F
#define SPURIOUS_REJECTION_RATIO (flags & SCAN_HIGH_SENSITIVITY ? HIGH_SPURIOUS_REJECTION_RATIO : LOW_SPURIOUS_REJECTION_RATIO)

// Local structures and variables

static struct biquad {
    float a0, a1, a2, b1, b2;	// coefficients
    float in_d1, in_d2;	        // delayed input
    float out_d1, out_d2;	    // delayed output
} bell_biquad;

// This structure represents a detected transient in the audio. We keep an array of these around by adding new
// transients to the end of the array and deleting expired ones off the beginning.

static struct peak {
    int time, area, width, height, filter_hits;
    float filtered_level;
} current_peak, peak_buffer [MAX_NUM_PEAKS];

// This array is used to calculate the amplitude sum of a window sliding over the normalized audio data.
// We use a rectangular window for computational efficiency.

static int16_t sample_window [WINDOW_SIZE];
static int num_peaks, sample_index;    
static float filtered_level;

// Local functions (except for Dbg_printf() which is external)

extern void Dbg_printf (const char *format, ...);
static char *time_format (int time_in_samples);
static void biquad_init (struct biquad *f, float gain, float a0, float a1, float a2, float b1, float b2);
static float biquad_apply (struct biquad *f, float input);
static void add_peak (struct peak *new_peak, int flags);
static int check_peaks (int flags);

// Initialize the audio scanner. Currently, all this does is initialize the biquad filter that is used to
// detect the bell. It should be a narrow bandpass tuned to the fundamental of the desired bell (not a
// harmonic). I measured my doorbell's frequency (the "ding", not the lower "dong") at 770 Hz and used
// the biquad generator at http://www.earlevel.com/main/2013/10/13/biquad-calculator-v2/ using a "Q" of
// 100. I measured a newer wireless doorbell (that only had a "ding") at 785 Hz and have included those
// coefficients also.

void scan_audio_init (void)
{
    biquad_init (&bell_biquad, 4.0F,
        0.0014867434962988915F, 0.0F, -0.0014867434962988915F, -1.9064233259820802F, 0.9970265130074023F    // 770 Hz, Q = 100
        // 0.001514749455122275F, 0.0F, -0.001514749455122275F, -1.9028338435963745F, 0.9969705010897554F      // 785 Hz, Q = 100
    );
}

// Scan the supplied mono audio samples and return any detected "knocks" or "rings". The "out_samples"
// array is used to capture various intermediate values of the stream processing for debugging purposes
// (this would probably not be used in the embedded version, but is available in this common code). The
// "flags" parameter specifies whether the high sensitivity mode is enabled, what intermediate values to
// write to the "out_samples" array, and the debug logging level (see scan.h).

int scan_audio (int16_t *in_samples, int num_samples, int16_t *out_samples, int flags)
{
    static float decorrelated_level = 32760.0F, peak_threshold = 30.0F;
    static int peak_started, window_index, window_sum;    
    static int16_t last_sample, weight;

    int detections = 0;

    while (num_samples--) {
        int16_t sample = *in_samples, window_level;
        float normalized_sample, filtered_sample;

        // The first operation on the audio is a trivial decorrelation. This basically just flatens
        // the spectrum of the signal, mostly in this case by reducing LF content which would make
        // transient detection more difficult.
  
        sample -= (weight * last_sample + 512) >> 10;

        if (sample && last_sample)
            weight += (((sample ^ last_sample) >> 30) | 1) << 1;

        last_sample = *in_samples++;

        if (out_samples && (flags & SCAN_OUTP_DECORR_AUDIO))
            *out_samples++ = sample;

        // Next we update a exponentially decaying average of the decorrelated audio's absolute
        // magnitude. The time constant is 256/16000 seconds, or 16 milliseconds.

        decorrelated_level = decorrelated_level * (255.0F / 256.0F) + abs (sample) * (1.0F / 256.0F);

        if (out_samples && (flags & SCAN_OUTP_DECORR_LEVEL))
            *out_samples++ = decorrelated_level;

        // Using the decaying average level, we normalize the decorrelated sample. Because the
        // average could go very low, we must clip this result (although this does not happen
        // often in practice). The decorrelated average level should never go to zero, but if
        // it did, that would cause an exception here.

        normalized_sample = sample / decorrelated_level * (float) NORMALIZATION_LEVEL;

        if (normalized_sample > 32760.0F)
            normalized_sample = 32760.0F;
        else if (normalized_sample < -32760.0F)
            normalized_sample = -32760.0F;

        if (out_samples && (flags & SCAN_OUTP_NORMAL_AUDIO))
            *out_samples++ = normalized_sample;

        // Finally we calculate the sum of the absolute normalized magnitudes inside of a sliding
        // rectangular window (ending at the current sample). We don't actually have to add up all
        // the values each time; instead we keep a running sum and simply subtract the expiring
        // value in the window and add the new value. Because this is a sum of normalized data,
        // it essentially represents the instantaneous level of change in the amplitude of the
        // signal, and so we subtract the normalization target value to create a signed value that
        // will be the basis of our transient detector (i.e., a sharp positive spike in this value
        // represents a transient).

        window_sum -= sample_window [window_index];
        window_sum += sample_window [window_index] = fabsf (normalized_sample);
        window_index = (window_index + 1) & WINDOW_MASK;
        window_level = ((window_sum + (WINDOW_SIZE/2)) >> WINDOW_BITS) - NORMALIZATION_LEVEL;

        if (out_samples && (flags & SCAN_OUTP_WINDOW_LEVEL))
            *out_samples++ = window_level;

        // Independent of the windowing stuff, we also filter the normalized audio with a biquad
        // bandpass tuned to the fundamental frequency of our target "bell", and then calculate
        // a exponentially decaying average on that signal. Because we specified an initial gain
        // of 4.0F when we initialized the filter, this can generate an max average level of 4
        // times the normalization level (assuming all the energy in the signal was at the
        // bandpass frequency). For normal broadband signals (i.e. no bell) this signal would
        // be much lower than the normalized audio level.

        filtered_sample = biquad_apply (&bell_biquad, normalized_sample);

        if (out_samples && (flags & SCAN_OUTP_FILTER_AUDIO)) {
            if (filtered_sample > 32760.0F)
                *out_samples++ = 32760;
            else if (filtered_sample < -32760.0F)
                *out_samples++ = -32760;
            else
                *out_samples++ = filtered_sample;
        }

       filtered_level = filtered_level * (255.0F / 256.0F) + fabsf (filtered_sample) * (1.0F / 256.0F);

        if (out_samples && (flags & SCAN_OUTP_FILTER_LEVEL))
            *out_samples++ = filtered_level;

        // Finally, we capture the potential transients. The algorithm is to keep track of every contiguous
        // region of positive windowed level (indicating that the average value in the window is greater
        // than the target normalization). For each of these areas we keep track of the maximum value (which
        // we call the peak's "height" and will also become the final transients "time") and the area under
        // the curve. When we have finished capturing the peak we will use the area and height to calculate
        // a virtual "width". This is more representative of the actual width because the signal may spend
        // a lot of time around zero and this would throw off the measurement.

        if (peak_started || window_level > 0) {
            if (!peak_started) {
                current_peak.filtered_level = filtered_level;
                current_peak.time = sample_index;
                current_peak.height = window_level;
                current_peak.area = window_level;
                current_peak.filter_hits = 0;
                peak_started++;
            }
            else if (window_level > current_peak.height) {
                current_peak.time = sample_index;
                current_peak.height = window_level;
            }
            else if (window_level <= 0) {
                peak_started = 0;

                // We have now captured a complete peak. To discriminate important peaks from background noise,
                // we keep a adjusting threshold value based on past history. The idea is to adjust this
                // threshold to allow a peak approximately every second (on average) and then use a second
                // "real" threshold that is a scaled version of the first. This allows our detector to adjust
                // to quiet environments by becoming more sensitive and avoid unnessary triggering in noisier
                // environments by becoming insensitive.

                if (current_peak.height > peak_threshold) {
                    peak_threshold *= 1.01F;    // bump threshold 1% each detected peak to target 1 per second

                    if (current_peak.height > peak_threshold * THRESHOLD_SCALING) {
                        current_peak.width = current_peak.area / current_peak.height;

                        if (flags & SCAN_DISP_PEAKS)
                            Dbg_printf ("peak added, time = %s, height = %d, width = %d, filtered level = %.2f\n",
                                time_format (current_peak.time), current_peak.height,
                                current_peak.width, current_peak.filtered_level);

                        add_peak (&current_peak, flags);
                    }
                }
            }
            else
                current_peak.area += window_level;
        }

        // We analyze the accumulated peaks at a fixed interval of 100 ms. By continuing to call check_peaks()
        // even when no new peaks have been added it allows us to observe the time period beyond the last
        // peak before issuing a detection, and it allows the peak buffer to be cleared of expired peaks.

        if (++sample_index % ANALYSIS_INTERVAL == 0) {
            detections |= check_peaks (flags);
            peak_threshold *= 0.999F;           // peak threshold decays about 1% per second
        }

        // Optionally display the peak thresholds every 10 seconds for debugging

        if ((flags & SCAN_DISP_THRESHOLDS) && sample_index % (SAMPLING_RATE * 10) == 0)
            Dbg_printf ("peak_threshold = %.2f base, %.2f actual\n", peak_threshold, peak_threshold * THRESHOLD_SCALING);

        // We work on a 24-hour loop for the sample_index, but we should only reset it when nothing's going on...

        if (sample_index > SAMPLING_RATE * 3600 * 24 && !num_peaks && !peak_started)
            sample_index %= SAMPLING_RATE * 3600 * 24;
    }

    return detections;
}

// Add the specified peak to the peak buffer. This could just be a simple copy operation except for the case where the
// peak buffer is full, in which case we need to remove the smallest peak first (or discard the new peak if it is the
// smallest). The "flags" are passed in just for debug logging output.

static void add_peak (struct peak *new_peak, int flags)
{
    if (num_peaks == MAX_NUM_PEAKS) {
        int i, smallest_peak_height = new_peak->height, smallest_peak_index = -1;

        for (i = 0; i < num_peaks; ++i)
            if (peak_buffer [i].height < smallest_peak_height) {
                smallest_peak_height = peak_buffer [i].height;
                smallest_peak_index = i;
            }

        if (smallest_peak_index == -1) {
            if (flags & SCAN_DISP_EVENTS)
                Dbg_printf ("add_peak(): discarded newest peak (height = %d) because buffer was full!\n", new_peak->height);

            return;
        }

        for (i = smallest_peak_index; i < num_peaks - 1; ++i)
            peak_buffer [i] = peak_buffer [i+1];

        if (flags & SCAN_DISP_EVENTS)
            Dbg_printf ("add_peak(): discarded smallest peak (height = %d) because buffer was full!\n", smallest_peak_height);

        num_peaks--;
    }

    peak_buffer [num_peaks++] = *new_peak;
}

// Check the current peak buffer for any "knocks" or "rings" that meet our defined parameters. The "flags" parameter is just
// used (for now) to control logging output and the high sensitivity mode. The return value indicates any detections. Note
// that any detections cause the peak buffer to be cleared so that we don't detect the same event again, although it could
// be problematic if we ever want to mask events at a higher level (e.g. a detected "knock" might wipe out a pending "ring").

static int check_peaks (int flags)
{
    int detections = 0;
    int p1, p2, p3, i;

    while (num_peaks && peak_buffer [0].time + KNOCK_MAX_SPAN * 2 < sample_index) {
        for (i = 0; i < num_peaks - 1; ++i)
            peak_buffer [i] = peak_buffer [i+1];
        num_peaks--;
    }

    for (p1 = 0; p1 < num_peaks - 2; ++p1)
        for (p2 = p1 + 1; p2 < num_peaks - 1; ++p2)
            for (p3 = p2 + 1; !detections && p3 < num_peaks; ++p3) {
                int span = peak_buffer [p3].time - peak_buffer [p1].time;

                if (span > KNOCK_MIN_SPAN && span < KNOCK_MAX_SPAN && 
                    peak_buffer [p1].width < 512 && peak_buffer [p2].width < 512 && peak_buffer [p3].width < 512 &&
                    peak_buffer [p3].time + (span / 2) < sample_index) {
                        int d1 = peak_buffer [p2].time - peak_buffer [p1].time;
                        int d2 = peak_buffer [p3].time - peak_buffer [p2].time;
                        float ratio = (d1 > d2) ? (float) d1 / d2 : (float) d2 / d1;
                        float min_height = peak_buffer [p1].height;

                        if (peak_buffer [p2].height < min_height) min_height = peak_buffer [p2].height;
                        if (peak_buffer [p3].height < min_height) min_height = peak_buffer [p3].height;

                        min_height = min_height * SPURIOUS_REJECTION_RATIO;

                        for (i = 0; i < num_peaks; ++i)
                            if (i != p1 && i != p2 && i != p3 &&
                                peak_buffer [i].time > peak_buffer [p1].time - (span / 3) &&
                                peak_buffer [i].time < peak_buffer [p3].time + (span / 3) &&
                                peak_buffer [i].height > min_height)
                                    break;

                        if (i == num_peaks && ratio < KNOCK_MAX_RATIO) {
                            if (flags & SCAN_DISP_EVENTS)
                                Dbg_printf ("*** knock detected, time = %s, span = %d, ratio = %.3f, heights = %d %d %d, widths = %d %d %d\n",
                                    time_format (peak_buffer [p1].time), d1 + d2, ratio,
                                    peak_buffer [p1].height, peak_buffer [p2].height, peak_buffer [p3].height,
                                    peak_buffer [p1].area / peak_buffer [p1].height, peak_buffer [p2].area / peak_buffer [p2].height,
                                    peak_buffer [p3].area / peak_buffer [p3].height);

                            detections |= SCAN_KNOCK_DETECTED;
                            num_peaks = 0;
                        }
                    }
            }

    for (p1 = 0; p1 < num_peaks; ++p1)
        if (peak_buffer [p1].time + SAMPLING_RATE > sample_index && filtered_level > peak_buffer [p1].filtered_level * 2 + 50)
            if (++peak_buffer [p1].filter_hits == 5) {
                if (flags & SCAN_DISP_EVENTS)
                    Dbg_printf ("*** ring detected, time = %s, delay = %.3f, pre level = %.2f, post level = %.2f\n",
                        time_format (peak_buffer [p1].time), (sample_index - peak_buffer [p1].time) / (float) SAMPLING_RATE,
                        peak_buffer [p1].filtered_level, filtered_level);

                detections |= SCAN_BELL_DETECTED;
                num_peaks = 0;
                break;
            }

    return detections;
}

// Initialize the specified biquad filter with the given parameters. Note that the "gain" parameter is supplied here
// to save a multiply every time the filter in applied.

static void biquad_init (struct biquad *f, float gain, float a0, float a1, float a2, float b1, float b2)
{
    f->a0 = a0 * gain;
    f->a1 = a1 * gain;
    f->a2 = a2 * gain;
    f->b1 = b1;
    f->b2 = b2;
    f->in_d1 = f->in_d2 = 0.0F;
    f->out_d1 = f->out_d2 = 0.0F;
}

// Apply the supplied sample to the specified biquad filter, which must have been initialized with biquad_init().

static float biquad_apply (struct biquad *f, float input)
{
    float sum = (input * f->a0) + (f->in_d1 * f->a1) + (f->in_d2 * f->a2) - (f->b1 * f->out_d1) - (f->b2 * f->out_d2);
    f->out_d2 = f->out_d1;
    f->out_d1 = sum;
    f->in_d2 = f->in_d1;
    f->in_d1 = input;
    return sum;
}

// Convert a sample index (at SAMPLING_RATE samples per second) to a formatted string in 24-hour time.
// Note that this returns a pointer to a static area and so is NOT re-entrant, and should not be used
// more than once in a single printf() statement!

static char *time_format (int time_in_samples)
{
    int hours = time_in_samples / (SAMPLING_RATE * 3600);
    int minutes = (time_in_samples / (SAMPLING_RATE * 60)) - (hours * 60);
    float seconds = (time_in_samples % (SAMPLING_RATE * 60)) / (float) SAMPLING_RATE;
    static char string [32];

    sprintf (string, "%02d:%02d:%06.3f", hours, minutes, seconds);
    return string;
}
