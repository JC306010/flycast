#pragma once
#include "types.h"

enum gd_states
{
	//Generic
	gds_waitcmd,
	gds_procata,
	gds_waitpacket,
	gds_procpacket,
	gds_pio_send_data,
	gds_pio_get_data,
	gds_pio_end,
	gds_procpacketdone,

	//Command spec.
	gds_readsector_pio,
	gds_readsector_dma,
	gds_process_set_mode,
};

//Structs & unions
struct SpiCommandInfo
{
	union
	{
		u8 CommandCode;
		u8 CommandData[12];
		u16 CommandData_16[6];
	};
};

struct GD_StatusT
{
	union
	{
		struct 
		{
			u32 CHECK:1;    //Bit 0 (CHECK) : Becomes "1" when an error has occurred during execution of the command the previous time.
			u32 :1;         //Bit 1         : Reserved
			u32 CORR:1;     //Bit 2 (CORR)  : Indicates that a correctable error has occurred. 
			u32 DRQ:1;      //Bit 3 (DRQ)   : Becomes "1" when preparations for data transfer between drive and host are completed. Information held in the Interrupt Reason Register becomes valid in the packet command when DRQ is set.
			u32 DSC:1;      //Bit 4 (DSC)   : Becomes "1" when seek processing is completed. 
			u32 DF:1;       //Bit 5 (DF)    : Returns drive fault information. 
			u32 DRDY:1;     //Bit 6 (DRDY)  : Set to "1" when the drive is able to respond to an ATA command. 
			u32 BSY:1;      //Bit 7 (BSY)   : BSY is always set to "1" when the drive accesses the command block. 
		};
		u8 full;
	};
};

struct GD_ErrRegT
{
	union
	{
		struct
		{
			u32 ILI:1;  //Bit 0 (ILI) : Command length is not correct (option). 
			u32 EOM:1;  //Bit 1 (EOM) : Media end was detected (option). 
			u32 ABRT:1; //Bit 2 (ABRT): Drive is not ready and command was made invalid (ATA level).
			u32 MCR:1;  //Bit 3 (MCR) : Media change was requested and media have been ejected (ATA level). 
			u32 Sense:4;//Bits 7 - 4  : Sense key. For details, refer to the Table 3.2. The Sense Key is only reflected in the SPI command mode, the same is true for ASC (Additional Sense Code), ASCQ (Additional Sense Code Qualifier). 
		};
		u8 full;
	};
};


struct GD_FeaturesT
{
	union
	{
		struct 
		{
			u32 DMA:1;//Bit 0 (DMA): Send data for command in DMA mode. 
			u32 :7;
		} CDRead;
		struct 
		{
			u32 FeatureNumber:7;//Bit 6 - 0 (Feature Number): Set transfer mode by setting to 3. 
			u32 Value :1;//not used
		} SetFeature;

		u8 full;
	};
};

struct GD_InterruptReasonT
{
	union
	{
		struct
		{
			u32 CoD:1; //Bit 0 (CoD) : "0" indicates data and "1" indicates a command. 
			u32 IO:1;  //Bit 1 (IO)  : "1" indicates transfer from device to host, and "0" from host to device.
			u32 :6;
		};
		u8 full;
	};
};
struct GD_SecCountT
{
	union
	{
		struct
		{
			u32 ModeVal:4;//Mode Value
			u32 TransMode:4;//Transfer Mode
		};
		u8 full;
	};
};
struct GD_SecNumbT
{
	union
	{
		struct
		{
			u32 Status:4;//Unit Status
			u32 DiscFormat:4;//DiskFormat
		};
		u8 full;
	};
};
extern GD_SecNumbT SecNumber;

struct read_params_t
{
	u32 start_sector;
	u32 remaining_sectors;
	u32 sector_type;
};

struct packet_cmd_t
{
	u32 index;
	union
	{
		u16 data_16[6];
		u8 data_8[12];
		//Spi command structs
		union
		{
			struct
			{
				u8 cc;

				u8 prmtype  : 1 ;
				u8 expdtype : 3 ;
				//	u8 datasel	: 4 ;
				u8 other    : 1 ; //"other" data. I guess that means SYNC/ECC/EDC ?
				u8 data     : 1 ; //user data. 2048 for mode1, 2048 for m2f1, 2324 for m2f2
				u8 subh     : 1 ; //8 bytes, mode2 subheader
				u8 head     : 1 ; //4 bytes, main CDROM header

				u8 block[10];
			};

			struct
			{
				u8 b[12];
			};
		}GDReadBlock;
	};
};

class DmaBuffer
{
public:
	DmaBuffer() {
		clear();
	}
	bool isEmpty() const {
		return size == 0;
	}
	u32 getSize() const {
		return size;
	}
	void clear() {
		size = 0;
		index = 0;
	}

	// Fill the cache with up to NSECT sectors if empty, using the passed read parameters
	void fill(read_params_t& params);
	// Return a pointer to the cache and advance the index by len bytes. len *must* be <= getSize()
	const u8 *read(u32 len);
	void serialize(Serializer& ser) const;
	void deserialize(Deserializer& deser);

private:
	static constexpr u32 NSECT = 16;
	u32 index;
	u32 size;
	u8 cache[2352 * NSECT] {};
};

class PioBuffer
{
public:
	PioBuffer() {
		clear();
	}
	bool isEmpty() const {
		return size == 0;
	}
	// Returns true if the buffer has reached its capacity and can't be read from or written to
	bool atEnd() const {
		return index == size;
	}
	// in bytes
	u32 getSize() const {
		return size * sizeof(u16);
	}
	void clear()
	{
		next_state = gds_waitcmd;
		size = 0;
		index = 0;
	}
	u16 read() {
		return _data[index++];
	}
	const u16 *data() const {
		return _data;
	}
	void write(u16 v) {
		_data[index++] = v;
	}
	// Returns a pointer to fill the buffer and sets its size. Index is reset.
	u16 *fill(u32 size) {
		resetSize(size);
		return &_data[0];
	}
	// Sets the buffer capacity and resets the index.
	void resetSize(u32 size) {
		this->index = 0;
		this->size = size / sizeof(u16);
	}
	void serialize(Serializer& ser) const;
	void deserialize(Deserializer& deser);

	gd_states next_state;
	static constexpr u32 Capacity = 64_KB;

private:
	u32 index;
	u32 size;
	u16 _data[Capacity / 2] {};
};

struct cdda_t
{
	enum { NoInfo, Playing, Paused, Terminated } status;
	u32 repeats;
	union
	{
		u32 FAD;
		struct
		{
			u8 B0; // MSB
			u8 B1; // Middle byte
			u8 B2; // LSB
		};
	}CurrAddr,EndAddr,StartAddr;
};
extern cdda_t cdda;

union ByteCount_t
{
	struct
	{
		u8 low;
		u8 hi;
	};

	u16 full;
};

// REQ_MODE / SET_MODE
struct GD_HardwareInfo_t
{
  u8 _res0[2];
  u8 speed;
  u8 _res1;
  u8 standby_hi;
  u8 standby_lo;
  u8 read_flags;
  u8 _res2[2];
  u8 read_retry;
  char drive_info[8];
  char system_version[8];
  char system_date[6];
};
extern GD_HardwareInfo_t GD_HardwareInfo;

extern int sns_asc;
extern int sns_ascq;
extern int sns_key;

#define GD_BUSY    0x00 // State transition
#define GD_PAUSE   0x01 // Pause
#define GD_STANDBY 0x02 // Standby (drive stop)
#define GD_PLAY    0x03 // CD playback
#define GD_SEEK    0x04 // Seeking
#define GD_SCAN    0x05 // Scanning
#define GD_OPEN    0x06 // Tray is open
#define GD_NODISC  0x07 // No disc
#define GD_RETRY   0x08 // Read retry in progress (option)
#define GD_ERROR   0x09 // Reading of disc TOC failed (state does not allow access)

//Response strings
extern const u16 reply_a1[];
extern const u16 reply_71[];

#define GD_IMPEDHI0_Read  0x005F7000 // (R) These are all 
#define GD_IMPEDHI4_Read  0x005F7004 // (R) RData bus high imped
#define GD_IMPEDHI8_Read  0x005F7008 // (R) For DIOR- ( READ ONLY )
#define GD_IMPEDHIC_Read  0x005F700C // (R) 

#define GD_ALTSTAT_Read   0x005F7018 // (R) |BSY|DRDY|DF|DSC|DRQ|CORR|Reserved|CHECK|
#define GD_DEVCTRL_Write  0x005F7018 // (W) Device Control |R|R|R|R|1|SRST|nIEN|0|

#define GD_DATA           0x005F7080 // (RW) Data / Data

#define GD_ERROR_Read     0x005F7084 // (R) Error |SENSE(4)|MCR|ABRT|EOMF|ILI|
#define GD_FEATURES_Write 0x005F7084 // (W) Features |Reserved(7)|DMA| or |SetClear|FeatNum(7)|

#define GD_IREASON_Read   0x005F7088 // (R) Interrupt Reason |Reserved(6)|IO|CoD|
#define GD_SECTCNT_Write  0x005F7088 // (W) Sector Count |XFerMode(4)|ModeVal(4)|


#define GD_SECTNUM        0x005F708C // (RW) Sector Number |DiscFmt(4)|Status(4)|
#define GD_BYCTLLO        0x005F7090 // (RW) 0x005F7090 Byte Control Low / Byte Control Low
#define GD_BYCTLHI        0x005F7094 // (RW) 0x005F7094 Byte Control High / Byte Control High

#define GD_DRVSEL         0x005F7098 // (RW) Unused |1|R|1|0|LUN(4)|

#define GD_STATUS_Read    0x005F709C // (R) |BSY|DRDY|DF|DSC|DRQ|CORR|Reserved|CHECK|
#define GD_COMMAND_Write  0x005F709C // (W) Command

// ATA Commands

#define ATA_NOP          0x00
#define ATA_SOFT_RESET   0x08
#define ATA_EXEC_DIAG    0x90
#define ATA_SPI_PACKET   0xA0
#define ATA_IDENTIFY_DEV 0xA1
#define ATA_IDENTIFY     0xEC
#define ATA_SET_FEATURES 0xEF


// SPI Packet Commands

#define SPI_TEST_UNIT 0x00 // 
#define SPI_REQ_STAT  0x10 // 
#define SPI_REQ_MODE  0x11 // 
#define SPI_SET_MODE  0x12 // 
#define SPI_REQ_ERROR 0x13 // 
#define SPI_GET_TOC   0x14 // 
#define SPI_REQ_SES   0x15 // 
#define SPI_CD_OPEN   0x16 // 
#define SPI_CD_PLAY   0x20 // 
#define SPI_CD_SEEK   0x21 // 
#define SPI_CD_SCAN   0x22 // 
#define SPI_CD_READ   0x30 // 
#define SPI_CD_READ2  0x31 // 
#define SPI_GET_SCD   0x40 // 
