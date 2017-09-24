#ifndef OPS_COMMON_H
#define OPS_COMMON_H
#include <stdint.h>

/**
 * repad_filename - change file name to 0xa0-padded format
 * @name: pointer to file name
 *
 * This function changes the file name at @name from a C string
 * to a 0xa0-padded file name.
 */
void repad_filename(uint8_t *name);

/**
 * terminate_filename - change 0xa0-padded file name to C string
 * @name: pointer to file name
 *
 * This function changes the file name at @name from 0xa0-padded format
 * to a C string.
 */
void terminate_filename(uint8_t *name);

#endif // OPS_COMMON_H
