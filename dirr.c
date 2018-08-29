	/* 
	dir [-df] [-o fmt] path
	format %d %b %e %s %c %m %a
	bcc32 -UW dir.c 
	cl dir.c
 */
/* #define _WIN32_WINNT 0x0400*/

#define UNICODE
#define _UNICODE

#include <tchar.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

/*
#include <openssl/md5.h>
*/

struct yfile *yfp;
TCHAR *line_fmt;

void memdump(const void *p, size_t len);

void
warn(TCHAR *format, ...) {
	va_list args;
	va_start(args, format);
	_ftprintf(stderr, _T("win32err=%u: "), (UINT)GetLastError());
	_vftprintf(stderr, format, args);
	_ftprintf(stderr, _T("\n"));
	va_end(args);
}

void
err(int exitcode, TCHAR *format, ...) {
	va_list args;
	va_start(args, format);
	warn(format, args);
	va_end(args);
	exit(exitcode);
}

//const int YFILE_BUF_SIZE = 65536;
#define YFILE_BUF_SIZE 65536
struct yfile {
	FILE *fp;
	HANDLE *h;
	UINT codepage;
	wchar_t bufW[YFILE_BUF_SIZE];
	char bufA[YFILE_BUF_SIZE*2];
	wchar_t *p;
};

void yfflush(struct yfile*);
void yfclose(struct yfile*);

struct yfile*
yfopen(TCHAR *filename) {
	struct yfile *yfp = malloc(sizeof(struct yfile));
	
	if (filename == NULL){
		yfp->fp = stdout;
		if (SetConsoleOutputCP(CP_UTF8) == 0) {
			_ftprintf(stderr, _T("SetConsoleOutputCP: win32err=%u: "), (UINT)GetLastError());
		}
	} else {
		yfp->fp = _tfopen(filename, _T("wb"));
		if (yfp->fp == NULL){
			free(yfp);
			return NULL;
		}
	}
	
	// SetConsoleOutputCP(CP_UTF8)すると、fwriteで以下エラーが発生することがある。
	// おそらく、UTF8の3バイト文字の途中で、stdioのバッファからwin32へ書き込まれたのが原因。
	// →stdioのバッファサイズを大きくしたら、発生しなくなった。
	// ERROR_OUTOFMEMORY
	// Not enough storage is available to complete this operation.
	int stdioBufSize = 65536;
	char *stdioBuf = malloc(stdioBufSize);
	setvbuf(yfp->fp, stdioBuf, _IOFBF, stdioBufSize);

	yfp->codepage = CP_UTF8;
	yfp->p = yfp->bufW;
	return yfp;
}

void
yfprintf(struct yfile *yfp, TCHAR *format, ...) {
	int n;
	DWORD m;
	va_list args;
	va_start(args, format);
	//_ftprintf(stderr, _T("yfprintf(): format=%s\n"), format); fflush(stderr);
	// memdump(yf->bufW, 16);
	_vsntprintf(yfp->bufW, sizeof(yfp->bufW), format, args);
	yfflush(yfp);
	va_end(args);
}

void
yfputc(TCHAR c, struct yfile *yfp) {
	yfprintf(yfp, _T("%c"), c);
}

void
yfflush(struct yfile *yfp) {
	int n;
	//_ftprintf(stderr, _T("yfprintf(): bufW=%s\n"), yf->bufW); fflush(stderr);
	// TODO 長さ0の文字列を渡すと戻り値はどうなる？？
	n = WideCharToMultiByte(yfp->codepage, 0, yfp->bufW, -1, yfp->bufA, sizeof(yfp->bufA), NULL, NULL);
	if (n==0) {
		err(1, _T("WideCharToMultiByte()"));
	}
	// memdump(yf->bufA, 16);
	//if (WriteFile(yf->h, yf->bufA, n - 1, &m, NULL)) 
	if(fwrite(yfp->bufA, 1, n - 1, yfp->fp) != n - 1){
		err(1, _T("WriteFile"));
	}
	
	yfp->p = yfp->bufW;
	*(yfp->p) = L'\0';
}

void
yfclose(struct yfile *yfp) {
	yfflush(yfp);
	fclose(yfp->fp);
	free(yfp);
}

void
printfiletime(FILETIME filetime){
	SYSTEMTIME systime;
	__int64 nsec;
	
	nsec = *((__int64*)(&filetime));
	nsec = (nsec % 10000000) * 100;
	FileTimeToSystemTime(&filetime, &systime);
	yfprintf(yfp, _T("%04d/%02d/%02d %02d:%02d:%02d.%09I64d"), 
		systime.wYear, systime.wMonth, systime.wDay,
		systime.wHour, systime.wMinute, systime.wSecond,
		nsec);
}

void
printfiletype(WIN32_FIND_DATA *ffd){
	// _ftprintf(stderr, _T("dwFileAttributes=%08x %s\n"), ffd->dwFileAttributes, ffd->cFileName);
	if (ffd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT){
		if (ffd->dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT){
			yfputc(_T('j'), yfp);
		} else if (ffd->dwReserved0 == IO_REPARSE_TAG_SYMLINK){
			yfputc(_T('l'), yfp);
		} else {
			_ftprintf(stderr, _T("unkown type: dwReserved0=%08x\n"), ffd->dwReserved0);
			yfputc(_T('?'), yfp);
		}
	} else if (ffd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
		yfputc(_T('d'), yfp);
	} else {
		// _ftprintf(stderr, _T("unkown type: dwFileAttributes=%08x\n"), ffd->dwFileAttributes);
		yfputc(_T('-'), yfp);
	}
}

/*
void
printmd5digest(unsigned char *md5_digest) {
	int i;
	_ftprintf(fp, _T("%02X"), md5_digest[0]);
	for (i = 1; i < MD5_DIGEST_LENGTH; i++) {
		_ftprintf(fp, _T(":%02X"), md5_digest[i]);
	}
}

void
printhash(TCHAR *path, TCHAR *filename, int filesize) {
	HANDLE hFile;
	HANDLE hMem;
	char *filedata;
	TCHAR fullpath[MAX_PATH];
	char md5_digest[MD5_DIGEST_LENGTH];
	
	_sntprintf(fullpath, MAX_PATH, _T("%s%s"), path, filename);

	if (filesize ==  0) {
		MD5(NULL, 0, md5_digest);
		printmd5digest(md5_digest);
		return;
	}
	
	hFile = CreateFile(fullpath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) {
		warn("CreateFile(): %s", fullpath);
	
	} else {
		hMem = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
		if (hMem == NULL){
			warn("CreateFileMapping(): %s", fullpath);
		} else {

			filedata = MapViewOfFileEx(hMem, FILE_MAP_READ, 
				0, 0, filesize, NULL);
			if (filedata == NULL){
				warn("MapViewOfFileEx(): filesize=%d: %s", filesize, fullpath);
			} else {
				// MD5
				MD5(filedata, filesize, md5_digest);
				printmd5digest(md5_digest);
				if (!UnmapViewOfFile(filedata)){
					warn("UnmapViewOfFile(): %s", fullpath);
				}
			}
			if (!CloseHandle(hMem)){
				warn("CloseHandle(): %s", fullpath);
			}
		}
		if (!CloseHandle(hFile)){
			warn(_T("CloseHandle(hFile): %s"), fullpath);
		}
	}

}
*/
void
printline(TCHAR *path, TCHAR *paTH, WIN32_FIND_DATA *FindFileData){
	TCHAR *p;

	if (FindFileData->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
		// ディレクトリの場合、表示しない
		return;
	}

	p = line_fmt;
	while (*p != _T('\0')){
		switch(*p){
		case '%':
			p++;
			switch(*p){
			case _T('\0'):
				_tprintf(_T("format err\n"));
				exit(1);
				break;
			case _T('t'):
				printfiletype(FindFileData);
				break;
			case _T('p'):
				// _ftprintf(fp, _T("%s"), path);
				yfprintf(yfp, _T("%s"), paTH);
				break;
			case _T('b'):
				yfprintf(yfp, _T("%s"), FindFileData->cFileName);
				break;
			case _T('s'):
				yfprintf(yfp, _T("%12llu"), ((int64_t)FindFileData->nFileSizeHigh << 32) + FindFileData->nFileSizeLow);
				break;
			case _T('c'):
				printfiletime(FindFileData->ftCreationTime);
				/*
				_ftprintf(fp, _T("%I64d"), FindFileData->ftCreationTime);
				*/
				break;
			case _T('m'):
				printfiletime(FindFileData->ftLastWriteTime);
				/*
				_ftprintf(fp, _T("%I64d"), FindFileData->ftLastWriteTime);
				*/
				break;
			case _T('a'):
				printfiletime(FindFileData->ftLastAccessTime);
				break;
			case _T('h'):
				//printhash(path, FindFileData->cFileName, FindFileData->nFileSizeLow);
				_tprintf(_T("hash function is not supported"));
				break;
			default:
				_tprintf(_T("format err\n"));
				exit(1);
				break;
			}
			break;
		default:
			yfputc(*p, yfp);
			break;
		}
		p++;
	}
	yfputc(_T('\n'), yfp);
	/*
	printfiletime(FindFileData->ftCreationTime);
	printfiletime(FindFileData->ftLastWriteTime);
	printfiletime(FindFileData->ftLastAccessTime);
	_ftprintf(fp, _T("%s\\%s#\n"), path, FindFileData->cFileName);
	*/
}

void
memdump(const void *p, size_t len) {
	int i;
	const unsigned char *q;
	q = p;
	for (i = 0; i < len; i++) {
		_tprintf(_T("%02x "), *q++ & 0xff); 
	}
	_tprintf(_T(";\n")); 
}

int
compare(const void *p, const void *q) {
	WIN32_FIND_DATA **pp, **qq;
	pp = (WIN32_FIND_DATA**) p;
	qq = (WIN32_FIND_DATA**) q;
	//memdump(p, 4);
	//memdump(q, 4);
	//_tprintf(_T("p=%p *p=%p\n"), p, *((char**)p)), 
	//_tprintf(_T("p=%s q=%s\n"), 
	//	(*pp)->cFileName, 
	//	(*qq)->cFileName);
	return _tcscmp(
		(*pp)->cFileName,
		(*qq)->cFileName);
}

void *
mallocx(size_t size) {
	void *p;
	p = malloc(size);
	memset(p, 0, size);
	return p;
}

void
recurse(TCHAR *path, TCHAR *paTH){
	HANDLE hFind;
	TCHAR path2[MAX_PATH];
	TCHAR paTH2[MAX_PATH];
	
	WIN32_FIND_DATA **list;
	void *blockList[10];
	WIN32_FIND_DATA *p;

	int listCount = 0;
	int listSize = 0;
	int blockCount = 0;
	int maxBlockCount = 10;
	
	int n;
	
	n = 10;
	listSize += n;
	list = mallocx(sizeof(WIN32_FIND_DATA*) * listSize);
	blockList[blockCount] = mallocx(sizeof(WIN32_FIND_DATA) * n);
		
	p = blockList[blockCount];
	// _tprintf(_T("list=%p blockList[%d]=%p\n"), list, blockCount, blockList[blockCount]);
	
	// 末尾に「*」を結合して、ファイル検索パターン文字列を作る
	_sntprintf(path2, MAX_PATH, _T("%s*"), path);

	/*
	_tprintf (_T("Target file is #%s#\n"), path2);
	*/

	// 最初のファイルを探す
	hFind = FindFirstFile(path2, p);
	if (hFind == INVALID_HANDLE_VALUE){
		_tprintf(_T("FindFirstFile(%s) %u\n"), path, (UINT)GetLastError());
		return;
	}

	list[listCount++] = p++;
	
	while (FindNextFile(hFind, p)) {
		list[listCount++] = p++;
		
		if (listCount == listSize) {
			if (blockCount == maxBlockCount) {
				_tprintf(_T("blockCount=%d maxBlockCount=%d\n"), blockCount, maxBlockCount);
				exit(1);
			}
			n = listSize * 2;
			listSize += n;
			list = realloc(list, sizeof(WIN32_FIND_DATA*) * listSize);
			blockCount++;
			blockList[blockCount] = mallocx(sizeof(WIN32_FIND_DATA) * n);
			p = blockList[blockCount];
			// _tprintf(_T("list=%p blockList[%d]=%p\n"), list, blockCount, blockList[blockCount]);
		}
	}

	if (GetLastError() != ERROR_NO_MORE_FILES) {
		// FindNextFile() がエラーだった場合
		_tprintf(_T("FindNextFile() %s, %d\n"), path, (UINT)GetLastError());
		// FindClose() をする必要があるので、ここで return しない。
	}
	
	if (FindClose(hFind) == 0) {
		_tprintf(_T("FindClose() %d\n"), (UINT)GetLastError());
		// ハンドルのクローズに失敗した場合、メモリリークになるが
		// とりあえず処理を続行する
	}

	qsort(list, listCount, sizeof(WIN32_FIND_DATA*), compare);
	
	int i;
	for (i = 0; i < listCount; i++) {
		p = list[i];

		// 見つかったファイルを表示する
		printline(path, paTH, p);

		if (p->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
			// 見つかったファイルがディレクトリの場合
			// TODO ジャンクション(UNIXのマウントポイント)の処理を追加
			if (_tcscmp(p->cFileName, _T(".")) != 0 &&
					_tcscmp(p->cFileName, _T("..")) != 0){
				// このディレクトリか親ディレクトリをあらわすファイル名で無い場合
				// 親ディレクトリと見つかったファイルのファイル名を結合する
				_sntprintf(path2, MAX_PATH, _T("%s%s\\"), path, p->cFileName);
				_sntprintf(paTH2, MAX_PATH, _T("%s%s/"), paTH, p->cFileName);
				// path2 以下を再帰的に探索して、結果出力用ファイルに書き出す
				recurse(path2, paTH2);
			} 
		}
	}
}

void
replaceChar(LPWSTR str, wchar_t c1, wchar_t c2)
{
	LPWSTR p;
	p = str;
	while ((p = wcschr(p, c1)) != NULL) {
		*p = c2;
		p++;
	}
}

void
usage(void){
	_ftprintf(stderr, _T("Usage: dir [-fd] [-F format] path"));
	exit(1);
}

#ifdef _UNICODE
int
main(int argc, char *argv[])
{
	int argcW;
	wchar_t **argvW;
	// LPWSTR argvW;
	LPWSTR cmdLine;

	cmdLine = GetCommandLineW();
	argvW = CommandLineToArgvW(cmdLine, &argcW);
	return _tmain(argcW, argvW);
}
#endif /* _UNICODE */

int
_tmain(int argc, TCHAR *argv[])
{
	int i;
	TCHAR path[MAX_PATH];
	TCHAR paTH[MAX_PATH];
	
	TCHAR *output_filename = NULL; //_T("dir.log");
#ifdef UNICODE
#ifdef BOM
	// ユニコード識別文字
	TCHAR bom;
	bom = 0xfeff;
#endif
#endif
	
	// line_fmt = _T("%s %c %m %a %p%b");
	// line_fmt = _T("%s\t%m\t%p%b");
	line_fmt = _T("%t 0 %s %m +0000 %p%b");
	
	// コマンドラインオプションの解析
	for (i = 1; i < argc; i++){
		if (argv[i][0] != _T('/') && argv[i][0] != _T('-')){
			break;
		}
		switch (argv[i][1]){
			case _T('f'):
				// ファイルのみ出力するオプション
				break;
			case _T('d'):
				// ディレクトリのみ出力するオプション
				break;
			case _T('F'):
				// 出力フォーマットを指定するオプション
				if (i + 1 == argc){
					_ftprintf(stderr, _T("This option need arg: %s\n"), argv[i]);
					usage();
				}
				i++;
				line_fmt = argv[i];
				break;
			case _T('o'):
				// 結果出力用ファイルのファイル名を指定するオプション
				if (i + 1 == argc){
					_ftprintf(stderr, _T("This option need arg: %s\n"), argv[i]);
					usage();
				}
				i++;
				output_filename = argv[i];
				break;
			default:
				// 無効なオプション
				_ftprintf(stderr, _T("Unknown option: %s\n"), argv[i]);
				usage();
				break;
		}
	}

	argc -= i - 1;
	argv += i - 1;
	if (argc == 1 || argv[1][0] == _T('\0')){
		// コマンドライン引数が無い
		path[0] = _T('\0');
		paTH[0] = _T('\0');
	}else if (argc != 2){
		// コマンドライン引数の数がおかしい
		usage();
	}else{
		// コマンドライン引数がある場合
		// TODO argv書き換えていいんだっけ？
		replaceChar(argv[1], _T('/'), _T('\\'));
		if (argv[1][_tcslen(argv[1]) - 1] != _T('\\')){
			// 末尾が「\」でない場合
			// 末尾に「\」を追加
			_sntprintf(path, sizeof(path) / sizeof(TCHAR), _T("%s\\"), argv[1]);
			_sntprintf(paTH, sizeof(paTH) / sizeof(TCHAR), _T("%s/"), argv[1]);
		}else{
			// 末尾が「\」の場合
			_sntprintf(path, sizeof(path) / sizeof(TCHAR), _T("%s"), argv[1]);
			_sntprintf(paTH, sizeof(paTH) / sizeof(TCHAR), _T("%s"), argv[1]);
		}
	}
	
	// パスの区切り文字を揃える
	replaceChar(paTH, _T('\\'), _T('/'));
	
	// 結果出力用ファイルを開く
	yfp = yfopen(output_filename);
	if (yfp == NULL){
		_tprintf(_T("can't open %s\n"), output_filename);
		exit(1);
	}

#ifdef UNICODE
#ifdef BOM
	// ユニコード識別文字(?)を出力
	yfprintf(yfp, _T("%c"), bom);
#endif
#endif	
	/*_setmode( _fileno( fp ), _O_BINARY );*/

	// path を再帰的に探索して、結果出力用ファイルに書き出す
	recurse(path, paTH);

	// 結果出力用ファイルを閉じる
	yfclose(yfp);

	// 正常終了
	return (0);
}
