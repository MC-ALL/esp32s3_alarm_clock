#ifndef ENVIRONMENT_BH1750_H_
#define ENVIRONMENT_BH1750_H_

int environment_bh1750_init(void);
void environment_bh1750_deinit(void);
int environment_bh1750_measure_lux(float *lux_out);

#endif
