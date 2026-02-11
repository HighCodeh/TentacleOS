#ifndef SUBGHZ_SPECTRUM_H
#define SUBGHZ_SPECTRUM_H

#define SPECTRUM_SAMPLES    80
extern float spectrum_data[SPECTRUM_SAMPLES];

void subghz_spectrum_start(void);
void subghz_spectrum_stop(void);

#endif // SUBGHZ_SPECTRUM_H

