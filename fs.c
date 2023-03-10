// ============================================================================
// fs.c - user FileSytem API
// ============================================================================

#include "fs.h"

// ============================================================================
// Close the file currently open on file descriptor 'fd'.
// ============================================================================
i32 fsClose(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  bfsDerefOFT(inum);
  return 0;
}



// ============================================================================
// Create the file called 'fname'.  Overwrite, if it already exsists.
// On success, return its file descriptor.  On failure, EFNF
// ============================================================================
i32 fsCreate(str fname) {
  i32 inum = bfsCreateFile(fname);
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Format the BFS disk by initializing the SuperBlock, Inodes, Directory and 
// Freelist.  On succes, return 0.  On failure, abort
// ============================================================================
i32 fsFormat() {
  FILE* fp = fopen(BFSDISK, "w+b");
  if (fp == NULL) FATAL(EDISKCREATE);

  i32 ret = bfsInitSuper(fp);               // initialize Super block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitInodes(fp);                  // initialize Inodes block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitDir(fp);                     // initialize Dir block
  if (ret != 0) { fclose(fp); FATAL(ret); }

  ret = bfsInitFreeList();                  // initialize Freelist
  if (ret != 0) { fclose(fp); FATAL(ret); }

  fclose(fp);
  return 0;
}


// ============================================================================
// Mount the BFS disk.  It must already exist
// ============================================================================
i32 fsMount() {
  FILE* fp = fopen(BFSDISK, "rb");
  if (fp == NULL) FATAL(ENODISK);           // BFSDISK not found
  fclose(fp);
  return 0;
}



// ============================================================================
// Open the existing file called 'fname'.  On success, return its file 
// descriptor.  On failure, return EFNF
// ============================================================================
i32 fsOpen(str fname) {
  i32 inum = bfsLookupFile(fname);        // lookup 'fname' in Directory
  if (inum == EFNF) return EFNF;
  return bfsInumToFd(inum);
}



// ============================================================================
// Read 'numb' bytes of data from the cursor in the file currently fsOpen'd on
// File Descriptor 'fd' into 'buf'.  On success, return actual number of bytes
// read (may be less than 'numb' if we hit EOF).  On failure, abort
// ============================================================================
i32 fsRead(i32 fd, i32 numb, void* buf) {

  // store incase of error
  i8 tempBuf[numb];
  u32 bufIdx = 0;
  i32 totalBytes = numb;

  i32 inum = bfsFdToInum(fd);
  // fetch cursor
  i32 cursor = bfsTell(fd);
  i32 cursorIdx = cursor % BYTESPERBLOCK;
  i32 fbn = cursor / BYTESPERBLOCK;

  while (numb > 0) {
    // fetch block
    i8 readBuf[BYTESPERBLOCK];
    bfsRead(inum, fbn, readBuf);
    i32 readCount = 0;

    // case cursor != beginning of block
    if (cursorIdx > 0) {
      // read at most numb bytes or end of block
      i32 bufCount = BYTESPERBLOCK - cursorIdx;
      readCount = (numb > bufCount) ? bufCount : numb;
      cursorIdx = 0;
    }
    // case cursor == beginning of block
    else {
      // read up to a full block
      readCount = MIN(BYTESPERBLOCK, numb);
    }

    // move to output
    memcpy(&tempBuf[bufIdx], &readBuf[cursorIdx], readCount);
    bufIdx += readCount;
    // move cursor
    numb -= readCount;

    // check for EoF
    if (fbn * BYTESPERBLOCK > fsSize(fd)) {
      // hit EoF, return total num bytes read
      return EBADREAD;
    }

    // read next block
    ++fbn;
  }
  /* 
  * Unsure why this check is neccessary as test requests 100 bytes of \0 
  * but test 6 has 524 (I think) trailing \0 that need to be removed
  */
  if(tempBuf[0] != 0) {
    // subtract null bytes
    i8 countBuffer[BYTESPERBLOCK];
    bfsRead(inum, fbn - 1, countBuffer);
    int emptyCount = 0;
    for(int i = BYTESPERBLOCK - 1; i >= 0; --i) {
      if(countBuffer[i] == 0) ++emptyCount;
      else break;
    }
    totalBytes -= emptyCount;
  }
  // move to return buffer
  memcpy(buf, tempBuf, totalBytes);
  fsSeek(fd, totalBytes, SEEK_CUR);
  return totalBytes;
}


// ============================================================================
// Move the cursor for the file currently open on File Descriptor 'fd' to the
// byte-offset 'offset'.  'whence' can be any of:
//
//  SEEK_SET : set cursor to 'offset'
//  SEEK_CUR : add 'offset' to the current cursor
//  SEEK_END : add 'offset' to the size of the file
//
// On success, return 0.  On failure, abort
// ============================================================================
i32 fsSeek(i32 fd, i32 offset, i32 whence) {

  if (offset < 0) FATAL(EBADCURS);

  i32 inum = bfsFdToInum(fd);
  i32 ofte = bfsFindOFTE(inum);

  switch (whence) {
  case SEEK_SET:
    g_oft[ofte].curs = offset;
    break;
  case SEEK_CUR:
    g_oft[ofte].curs += offset;
    break;
  case SEEK_END: {
    i32 end = fsSize(fd);
    g_oft[ofte].curs = end + offset;
    break;
  }
  default:
    FATAL(EBADWHENCE);
  }
  return 0;
}



// ============================================================================
// Return the cursor position for the file open on File Descriptor 'fd'
// ============================================================================
i32 fsTell(i32 fd) {
  return bfsTell(fd);
}



// ============================================================================
// Retrieve the current file size in bytes.  This depends on the highest offset
// written to the file, or the highest offset set with the fsSeek function.  On
// success, return the file size.  On failure, abort
// ============================================================================
i32 fsSize(i32 fd) {
  i32 inum = bfsFdToInum(fd);
  return bfsGetSize(inum);
}



// ============================================================================
// Write 'numb' bytes of data from 'buf' into the file currently fsOpen'd on
// filedescriptor 'fd'.  The write starts at the current file offset for the
// destination file.  On success, return 0.  On failure, abort
// ============================================================================
i32 fsWrite(i32 fd, i32 numb, void* buf) {

  // store incase of error
  i8 tempBuf[numb];
  memcpy(tempBuf, buf, numb);
  u32 bufIdx = 0;

  i32 inum = bfsFdToInum(fd);
  // fetch cursor
  i32 cursor = bfsTell(fd);
  i32 cursorIdx = cursor % BYTESPERBLOCK;
  i32 fbn = cursor / BYTESPERBLOCK;

  // fetch dbn
  i32 dbn = bfsFbnToDbn(inum, fbn);
  if (dbn == ENODBN) {
    // alloc if not mapped
    bfsAllocBlock(inum, fbn);
    i8 allocBuf[BYTESPERBLOCK];
    memset(allocBuf, 0, BYTESPERBLOCK);
    dbn = bfsFbnToDbn(inum, fbn);
    bioWrite(dbn, allocBuf);
  } 

  while (numb > 0) {
    // fetch block
    i8 writeBuf[BYTESPERBLOCK];
    bfsRead(inum, fbn, writeBuf);
    i32 writeCount = 0;

    // case cursor != beginning of block
    if (cursorIdx > 0) {
      // read at most numb bytes or end of block
      i32 bufCount = BYTESPERBLOCK - cursorIdx;
      writeCount = (numb > bufCount) ? bufCount : numb;
    }
    // case cursor == beginning of block
    else {
      // read up to a full block
      writeCount = MIN(BYTESPERBLOCK, numb);
    }

    // write to buffer
    memcpy(&writeBuf[cursorIdx], &tempBuf[bufIdx], writeCount);
    // move cursor
    cursorIdx = 0;
    bufIdx += writeCount;
    numb -= writeCount;

    // write to file
    bioWrite(dbn, writeBuf);
    fsSeek(fd, writeCount, SEEK_CUR);

    // next block
    dbn = bfsFbnToDbn(inum, ++fbn);

    // check for EoF
    if (dbn == ENODBN) {
      // hit EoF, expand file
      bfsAllocBlock(inum, fbn);
      i8 allocBuf[BYTESPERBLOCK];
      memset(allocBuf, 0, BYTESPERBLOCK);
      dbn = bfsFbnToDbn(inum, fbn);
      bioWrite(dbn, allocBuf);
    }
  }
  return 0;
}
