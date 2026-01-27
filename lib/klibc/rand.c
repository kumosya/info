#include <stdlib.h>

/*
 * rand - Generate random number
 * Returns a random integer between 0 and RAND_MAX
 */

/* Seed for the random number generator */
static unsigned long next = 1;

int rand(void)
{
    /* Simple linear congruential generator */
    /* This is not a high-quality RNG, but it's simple */
    next = next * 1103515245 + 12345;
    return (unsigned int)(next / 65536) % (RAND_MAX + 1);
}

/*
 * srand - Initialize random number generator
 * Sets the seed for the random number generator
 */
void srand(unsigned int seed)
{
    next = seed;
}