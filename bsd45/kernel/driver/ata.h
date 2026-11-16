/*
 * ata.h - minimal polling PIO ATA disk interface (read + write, LBA28).
 */
#ifndef _ATA_H_
#define _ATA_H_

int	ata_probe(void);
void	ata_reset(void);
int	ata_present(void);
unsigned ata_capacity(void);
int	ata_read(unsigned lba, unsigned nsec, void *buf);
int	ata_write(unsigned lba, unsigned nsec, const void *buf);

#endif /* _ATA_H_ */
