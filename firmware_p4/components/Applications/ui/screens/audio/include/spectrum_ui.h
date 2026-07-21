// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef SPECTRUM_UI_H
#define SPECTRUM_UI_H

/**
 * @brief Open the live audio spectrum analyzer: a background task streams the
 *        PDM mic, runs a real FFT (esp-dsp) and feeds AGC-normalized frequency
 *        bands to an animated bar display. BACK returns to Settings.
 */
void ui_spectrum_open(void);

#endif // SPECTRUM_UI_H
