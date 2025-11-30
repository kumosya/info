#include <stdlib.h>

/*
 * bsearch - Binary search
 * Searches an array for an element matching key using the binary search algorithm
 * Returns a pointer to the element, or NULL if not found
 */
void *bsearch(const void *key, const void *base, 
             size_t nmemb, size_t size, 
             int (*compar)(const void *, const void *))
{
    size_t low = 0;
    size_t high = nmemb - 1;
    size_t mid;
    const char *base_char = (const char *)base;
    int cmp_result;
    
    if (key == NULL || base == NULL || compar == NULL || nmemb == 0) {
        return NULL;
    }
    
    /* Binary search loop */
    while (low <= high) {
        mid = (low + high) / 2;
        const void *mid_element = base_char + mid * size;
        
        cmp_result = compar(key, mid_element);
        
        if (cmp_result < 0) {
            /* Key is less than mid_element, search lower half */
            if (mid == 0) break;  /* Prevent underflow */
            high = mid - 1;
        } else if (cmp_result > 0) {
            /* Key is greater than mid_element, search upper half */
            low = mid + 1;
        } else {
            /* Found the element */
            return (void *)mid_element;
        }
    }
    
    /* Element not found */
    return NULL;
}