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

#include "imu_orientation.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PI 3.14159265358979323846f
static float error(float a,float b) { return fabsf(atan2f(sinf((a-b)*PI/180),cosf((a-b)*PI/180))*180/PI); }
static void spin(int axis,float speed,int steps,imu_orientation_t *s) {
  for(int i=1;i<=steps;++i) {
    float rad=i*speed*.005f*PI/180, a[3]={0,0,1},g[3]={0};g[axis]=speed;
    if(axis==0) { a[1]=sinf(rad);a[2]=cosf(rad); }
    if(axis==1) { a[0]=-sinf(rad);a[2]=cosf(rad); }
    assert(imu_orientation_update(s,a,g,.005f));
  }
}
int main(void) {
  for(int axis=0;axis<3;++axis) for(int sign=-1;sign<=1;sign+=2) {
    imu_orientation_t s;imu_orientation_reset(&s);
    float a[3]={0,0,1},g[3]={0},m[9],angle[3];
    assert(imu_orientation_update(&s,a,g,.005f));spin(axis,sign*60,200,&s);
    imu_orientation_matrix(&s,m,angle);
    assert(error(angle[axis],sign*60)<.4f);
    for(int j=0;j<3;++j) if(j!=axis) assert(fabsf(angle[j])<.4f);
    imu_orientation_recenter(&s);imu_orientation_matrix(&s,m,angle);
    for(int j=0;j<3;++j) assert(fabsf(angle[j])<.001f);
  }
  puts("PASS: six signed rotations and recenter");
  imu_orientation_t s;imu_orientation_reset(&s);
  float a[3]={0,0,1},g[3]={0},m[9],angle[3];
  imu_orientation_update(&s,a,g,.005f);spin(2,90,800,&s);
  imu_orientation_matrix(&s,m,angle);assert(error(angle[2],0)<.1f);
  float n=0;for(int i=0;i<4;++i)n+=s.q[i]*s.q[i];assert(fabsf(n-1)<.00001f);
  puts("PASS: full turn, normalized quaternion without Euler integration");
  for(int axis=0;axis<3;++axis) {
    imu_orientation_reset(&s);imu_orientation_update(&s,a,g,.005f);spin(axis,90,800,&s);
    imu_orientation_matrix(&s,m,angle);
    for(int j=0;j<3;++j) assert(error(angle[j],0)<.6f);
  }
  puts("PASS: continuous 360-degree roll/pitch/yaw through inverted poses");
  imu_orientation_t before=s;
  assert(!imu_orientation_update(&s,a,g,1));assert(memcmp(&before,&s,sizeof(s))==0);
  a[0]=NAN;assert(!imu_orientation_update(&s,a,g,.01f));assert(memcmp(&before,&s,sizeof(s))==0);
  a[0]=0;a[2]=2;for(int i=0;i<500;++i)imu_orientation_update(&s,a,g,.005f);
  imu_orientation_matrix(&s,m,angle);assert(error(angle[2],0)<.1f);
  puts("PASS: gap/NaN rejection and acceleration-magnitude gating");
  imu_orientation_reset(&s);a[2]=-1;imu_orientation_update(&s,a,g,.01f);
  for(int i=0;i<1000;++i)imu_orientation_update(&s,a,g,.01f);
  imu_orientation_matrix(&s,m,angle);for(int i=0;i<3;++i)assert(fabsf(angle[i])<.001f);
  puts("PASS: inverted startup has no singularity");
  imu_orientation_reset(&s);a[2]=1;imu_orientation_update(&s,a,g,.01f);
  s.q[0]=cosf(.1f);s.q[1]=sinf(.1f);
  for(int i=0;i<1000;++i)imu_orientation_update(&s,a,g,.01f);
  imu_orientation_matrix(&s,m,angle);assert(fabsf(angle[0])<.01f);
  puts("PASS: gravity feedback converges toward level, not away");
  imu_zero_t z;imu_zero_reset(&z);float bias[3]={99,99,99};g[0]=-3;g[1]=2.6;g[2]=-.4;
  int result=0;for(int i=0;i<IMU_ZERO_SAMPLES;++i)result=imu_zero_sample(&z,a,g,bias);
  assert(result==1);for(int j=0;j<3;++j)assert(fabsf(bias[j]-g[j])<.001f);
  float saved[3];memcpy(saved,bias,sizeof(bias));
  for(int i=0;i<IMU_ZERO_SAMPLES;++i){g[0]=(i%2)*4;result=imu_zero_sample(&z,a,g,bias);}
  assert(result==-1);assert(memcmp(saved,bias,sizeof(bias))==0);
  a[0]=NAN;assert(imu_zero_sample(&z,a,g,bias)==-1);assert(z.count==0);
  puts("PASS: static offset calibrated, moving/invalid windows preserve bias");
  return 0;
}
