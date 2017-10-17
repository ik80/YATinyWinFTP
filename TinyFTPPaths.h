#ifndef IK80_TINYFTPPATHS_H_
#define IK80_TINYFTPPATHS_H_

namespace TinyWinFTP
{
	bool SetRootDir(char * dir);
	bool MySetDir(char * dir);
	char * MyGetDir(void);
	char * TranslatePath(const char * FilePath);
}

#endif // IK80_TINYFTPPATHS_H_