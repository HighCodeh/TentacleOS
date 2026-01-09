#ifndef DEAUTHER_DETECTOR_H
#define DEAUTHER_DETECTOR_H

#include <stdint.h>

/**
 * @brief Starts the deauther detector.
 * 
 * Takes the WiFi interface into promiscuous mode, starts a channel hopping task
 * (allocated in PSRAM) and counts deauthentication frames (counter in PSRAM).
 */
void deauther_detector_start(void);

/**
 * @brief Stops the deauther detector.
 * 
 * Disables promiscuous mode and deletes the channel hopping task.
 */
void deauther_detector_stop(void);

/**
 * @brief Get the current deauth packet count.
 * 
 * @return uint32_t Number of deauth packets detected.
 */
uint32_t deauther_detector_get_count(void);

#endif // DEAUTHER_DETECTOR_H
