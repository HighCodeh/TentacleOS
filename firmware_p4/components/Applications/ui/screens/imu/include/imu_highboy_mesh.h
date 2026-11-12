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

#ifndef IMU_HIGHBOY_MESH_H
#define IMU_HIGHBOY_MESH_H
#include <stdint.h>
#define IMU_MESH_MAGIC       0x314D4248U
#define IMU_MESH_VERTICES    581U
#define IMU_MESH_FACES       885U
#define IMU_MESH_COLORS      5U
#define IMU_MESH_COORD_LIMIT 2000
#ifndef IMU_MESH_PATH
#define IMU_MESH_PATH "/assets/models/highboy_imu.bin"
#endif
typedef struct {
  uint16_t a, b, c;
  uint8_t material, reserved;
} imu_mesh_face_t;
typedef struct {
  int16_t vertices[IMU_MESH_VERTICES][3];
  imu_mesh_face_t faces[IMU_MESH_FACES];
  uint32_t palette[IMU_MESH_COLORS];
} imu_mesh_t;
#endif
