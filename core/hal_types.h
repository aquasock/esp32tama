/*
 * hal_types.h - integer types for TamaLib on this project
 *
 * Generated from TamaLib's hal_types.h.template. TamaLib is built to run on
 * 8-bit microcontrollers, so it does not use the standard integer names
 * directly; these are the widths the CPU needs.
 *
 * Note the timestamp unit: TamaLib is initialised with a frequency and calls
 * the HAL's get_timestamp()/sleep_until() in units of that frequency. Here the
 * frequency is 1,000,000 and timestamps are microseconds. A 32-bit
 * microsecond counter wraps every ~71 minutes; TamaLib is written to handle
 * wrapping (see the note in the template), so that is safe.
 */
#ifndef _HAL_TYPES_H_
#define _HAL_TYPES_H_

typedef unsigned char      bool_t;
typedef unsigned char      u4_t;
typedef unsigned char      u5_t;
typedef unsigned char      u8_t;
typedef unsigned short     u12_t;
typedef unsigned short     u13_t;
typedef unsigned int       u32_t;
typedef unsigned int       timestamp_t;   /* unsigned, so wrapping is well defined */

#endif /* _HAL_TYPES_H_ */
