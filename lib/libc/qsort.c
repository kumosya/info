#include <stdlib.h>

/*
 * qsort - Sort an array
 * Sorts the array pointed to by base into ascending order according to
 * the comparison function pointed to by compar
 */

/* Helper function to swap two elements */
static void swap(char *a, char *b, size_t size)
{
    size_t i;
    char temp;
    
    for (i = 0; i < size; i++) {
        temp = a[i];
        a[i] = b[i];
        b[i] = temp;
    }
}

/* Quick sort implementation */
static void quick_sort(char *base, size_t nmemb, size_t size, 
                      int (*compar)(const void *, const void *))
{
    if (nmemb <= 1) {
        return;  /* Already sorted */
    }
    
    /* Choose pivot (using the middle element) */
    size_t pivot_index = nmemb / 2;
    char *pivot = base + pivot_index * size;
    
    /* Move pivot to the end */
    swap(pivot, base + (nmemb - 1) * size, size);
    pivot = base + (nmemb - 1) * size;
    
    /* Partition */
    size_t i, j = 0;
    for (i = 0; i < nmemb - 1; i++) {
        if (compar(base + i * size, pivot) <= 0) {
            swap(base + i * size, base + j * size, size);
            j++;
        }
    }
    
    /* Move pivot back to its correct position */
    swap(base + j * size, pivot, size);
    
    /* Recursively sort the two partitions */
    quick_sort(base, j, size, compar);
    quick_sort(base + (j + 1) * size, nmemb - j - 1, size, compar);
}

void qsort(void *base, size_t nmemb, size_t size, 
          int (*compar)(const void *, const void *))
{
    if (base == NULL || compar == NULL || nmemb <= 1) {
        return;
    }
    
    quick_sort((char *)base, nmemb, size, compar);
}