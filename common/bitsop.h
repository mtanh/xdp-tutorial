#ifndef __BITSOP__
#define __BITSOP__

//
// bit=2^x, where x is from 0 to 31 (inclusive)
//

#define IS_BIT_SET(n, bit) (n & bit)
#define TOGGLE_BIT(n, bit) (n = (n ^ (bit)))
#define COMPLEMENT(n) (n = (n ^ 0xFFFFFFFF))
#define UNSET_BIT(n, bit) (n = (n & ((bit) ^ 0xFFFFFFFF)))
#define SET_BIT(n, bit) (n = (n | (bit)))

#endif /* __BITSOP__  */
