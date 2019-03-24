//
// OS specific CDROM interface (Mac OS X)
//
// by James L. Hammons & ?
//

//
// OS X support functions
// OS specific implementation of OS agnostic functions
//

uint8_t CDIntfInit(void)
{
	WriteLog("CDINTF: Init unimplemented!\n");
	return false;
}

void CDIntfDone(void)
{
}

uint8_t CDIntfReadBlock(uint32_t sector, uint8_t * buffer)
{
	WriteLog("CDINTF: ReadBlock unimplemented!\n");
	return false;
}

uint32_t CDIntfGetNumSessions(void)
{
	// Still need relevant code here... !!! FIX !!!
	return 2;
}

void CDIntfSelectDrive(uint32_t driveNum)
{
	WriteLog("CDINTF: SelectDrive unimplemented!\n");
}

uint32_t CDIntfGetCurrentDrive(void)
{
	WriteLog("CDINTF: GetCurrentDrive unimplemented!\n");
	return 0;
}

const uint8_t * CDIntfGetDriveName(uint32_t)
{
	WriteLog("CDINTF: GetDriveName unimplemented!\n");
	return NULL;
}

uint8_t CDIntfGetSessionInfo(uint32_t session, uint32_t offset)
{
	WriteLog("CDINTF: GetSessionInfo unimplemented!\n");
	return 0xFF;
}

uint8_t CDIntfGetTrackInfo(uint32_t track, uint32_t offset)
{
	WriteLog("CDINTF: GetTrackInfo unimplemented!\n");
	return 0xFF;
}
