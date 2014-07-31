// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// An Env is an interface used by the kudu implementation to access
// operating system functionality like the filesystem etc.  Callers
// may wish to provide a custom Env object when opening a database to
// get fine gain control; e.g., to rate limit file system operations.
//
// All Env implementations are safe for concurrent access from
// multiple threads without any external synchronization.

#ifndef STORAGE_LEVELDB_INCLUDE_ENV_H_
#define STORAGE_LEVELDB_INCLUDE_ENV_H_

#include <stdint.h>
#include <cstdarg>
#include <string>
#include <vector>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/util/status.h"

namespace kudu {

class FileLock;
class RandomAccessFile;
class SequentialFile;
class Slice;
class WritableFile;
struct WritableFileOptions;

class Env {
 public:
  Env() { }
  virtual ~Env();

  // Return a default environment suitable for the current operating
  // system.  Sophisticated users may wish to provide their own Env
  // implementation instead of relying on this default environment.
  //
  // The result of Default() belongs to kudu and must never be deleted.
  static Env* Default();

  // Create a brand new sequentially-readable file with the specified name.
  // On success, stores a pointer to the new file in *result and returns OK.
  // On failure stores NULL in *result and returns non-OK.  If the file does
  // not exist, returns a non-OK status.
  //
  // The returned file will only be accessed by one thread at a time.
  virtual Status NewSequentialFile(const std::string& fname,
                                   SequentialFile** result) = 0;

  // Create a brand new random access read-only file with the
  // specified name.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores NULL in *result and
  // returns non-OK.  If the file does not exist, returns a non-OK
  // status.
  //
  // The returned file may be concurrently accessed by multiple threads.
  virtual Status NewRandomAccessFile(const std::string& fname,
                                     RandomAccessFile** result) = 0;

  // Create an object that writes to a new file with the specified
  // name.  Deletes any existing file with the same name and creates a
  // new file.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores NULL in *result and
  // returns non-OK.
  //
  // The returned file will only be accessed by one thread at a time.
  virtual Status NewWritableFile(const std::string& fname,
                                 WritableFile** result) = 0;


  // Like the previous NewWritableFile, but allows options to be
  // specified.
  virtual Status NewWritableFile(const WritableFileOptions& opts,
                                 const std::string& fname,
                                 WritableFile** result) = 0;

  // Creates a new WritableFile provided the name_template parameter.
  // The last six characters of name_template must be "XXXXXX" and these are
  // replaced with a string that makes the filename unique.
  // The resulting created filename, if successful, will be stored in the
  // created_filename out parameter.
  // The file is created with permissions 0600, that is, read plus write for
  // owner only. The implementation will create the file in a secure manner,
  // and will return an error Status if it is unable to open the file.
  virtual Status NewTempWritableFile(const WritableFileOptions& opts,
                                     const std::string& name_template,
                                     std::string* created_filename,
                                     gscoped_ptr<WritableFile>* result) = 0;

  // Returns true iff the named file exists.
  virtual bool FileExists(const std::string& fname) = 0;

  // Store in *result the names of the children of the specified directory.
  // The names are relative to "dir".
  // Original contents of *results are dropped.
  virtual Status GetChildren(const std::string& dir,
                             std::vector<std::string>* result) = 0;

  // Delete the named file.
  virtual Status DeleteFile(const std::string& fname) = 0;

  // Create the specified directory.
  virtual Status CreateDir(const std::string& dirname) = 0;

  // Delete the specified directory.
  virtual Status DeleteDir(const std::string& dirname) = 0;

  // Synchronize the entry for a specific directory.
  virtual Status SyncDir(const std::string& dirname) = 0;

  // Recursively delete the specified directory.
  // This should operate safely, not following any symlinks, etc.
  virtual Status DeleteRecursively(const std::string &dirname) = 0;

  // Store the size of fname in *file_size.
  virtual Status GetFileSize(const std::string& fname, uint64_t* file_size) = 0;

  // Rename file src to target.
  virtual Status RenameFile(const std::string& src,
                            const std::string& target) = 0;

  // Lock the specified file.  Used to prevent concurrent access to
  // the same db by multiple processes.  On failure, stores NULL in
  // *lock and returns non-OK.
  //
  // On success, stores a pointer to the object that represents the
  // acquired lock in *lock and returns OK.  The caller should call
  // UnlockFile(*lock) to release the lock.  If the process exits,
  // the lock will be automatically released.
  //
  // If somebody else already holds the lock, finishes immediately
  // with a failure.  I.e., this call does not wait for existing locks
  // to go away.
  //
  // May create the named file if it does not already exist.
  virtual Status LockFile(const std::string& fname, FileLock** lock) = 0;

  // Release the lock acquired by a previous successful call to LockFile.
  // REQUIRES: lock was returned by a successful LockFile() call
  // REQUIRES: lock has not already been unlocked.
  virtual Status UnlockFile(FileLock* lock) = 0;

  // *path is set to a temporary directory that can be used for testing. It may
  // or many not have just been created. The directory may or may not differ
  // between runs of the same process, but subsequent calls will return the
  // same directory.
  virtual Status GetTestDirectory(std::string* path) = 0;

  // Returns the number of micro-seconds since some fixed point in time. Only
  // useful for computing deltas of time.
  virtual uint64_t NowMicros() = 0;

  // Sleep/delay the thread for the perscribed number of micro-seconds.
  virtual void SleepForMicroseconds(int micros) = 0;

  // Get caller's thread id.
  virtual uint64_t gettid() = 0;

  // Return the full path of the currently running executable.
  virtual Status GetExecutablePath(std::string* path) = 0;

 private:
  // No copying allowed
  Env(const Env&);
  void operator=(const Env&);
};

// A file abstraction for reading sequentially through a file
class SequentialFile {
 public:
  SequentialFile() { }
  virtual ~SequentialFile();

  // Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
  // written by this routine.  Sets "*result" to the data that was
  // read (including if fewer than "n" bytes were successfully read).
  // May set "*result" to point at data in "scratch[0..n-1]", so
  // "scratch[0..n-1]" must be live when "*result" is used.
  // If an error was encountered, returns a non-OK status.
  //
  // REQUIRES: External synchronization
  virtual Status Read(size_t n, Slice* result, uint8_t *scratch) = 0;

  // Skip "n" bytes from the file. This is guaranteed to be no
  // slower that reading the same data, but may be faster.
  //
  // If end of file is reached, skipping will stop at the end of the
  // file, and Skip will return OK.
  //
  // REQUIRES: External synchronization
  virtual Status Skip(uint64_t n) = 0;
};

// A file abstraction for randomly reading the contents of a file.
class RandomAccessFile {
 public:
  RandomAccessFile() { }
  virtual ~RandomAccessFile();

  // Read up to "n" bytes from the file starting at "offset".
  // "scratch[0..n-1]" may be written by this routine.  Sets "*result"
  // to the data that was read (including if fewer than "n" bytes were
  // successfully read).  May set "*result" to point at data in
  // "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
  // "*result" is used.  If an error was encountered, returns a non-OK
  // status.
  //
  // Safe for concurrent use by multiple threads.
  virtual Status Read(uint64_t offset, size_t n, Slice* result,
                      uint8_t *scratch) const = 0;

  // Returns the size of the file
  virtual Status Size(uint64_t *size) const = 0;
};

// Creation-time options for WritableFile
struct WritableFileOptions {
  // Use memory-mapped I/O if supported.
  bool mmap_file;

  // Call Sync() during Close().
  bool sync_on_close;

  WritableFileOptions()
    : mmap_file(true),
      sync_on_close(false) { }
};

// A file abstraction for sequential writing.  The implementation
// must provide buffering since callers may append small fragments
// at a time to the file.
class WritableFile {
 public:
  WritableFile() { }
  virtual ~WritableFile();

  // Pre-allocates 'size' bytes for the file in the underlying filesystem.
  // size bytes are added to the current pre-allocated size or to the current
  // offset, whichever is bigger. In no case is the file truncated by this
  // operation.
  virtual Status PreAllocate(uint64_t size) = 0;
  virtual Status Append(const Slice& data) = 0;

  // If possible, uses scatter-gather I/O to efficiently append
  // multiple buffers to a file. Otherwise, falls back to regular I/O.
  //
  // For implementation specific quirks and details, see comments in
  // implementation source code (e.g., env_posix.cc)
  virtual Status AppendVector(const std::vector<Slice>& data_vector) = 0;
  virtual Status Close() = 0;
  virtual Status Flush() = 0;
  virtual Status Sync() = 0;
  virtual uint64_t Size() const = 0;

 private:
  // No copying allowed
  WritableFile(const WritableFile&);
  void operator=(const WritableFile&);
};


// Identifies a locked file.
class FileLock {
 public:
  FileLock() { }
  virtual ~FileLock();
 private:
  // No copying allowed
  FileLock(const FileLock&);
  void operator=(const FileLock&);
};

// A utility routine: write "data" to the named file.
extern Status WriteStringToFile(Env* env, const Slice& data,
                                const std::string& fname);

// A utility routine: read contents of named file into *data
extern Status ReadFileToString(Env* env, const std::string& fname,
                               faststring* data);

// An implementation of Env that forwards all calls to another Env.
// May be useful to clients who wish to override just part of the
// functionality of another Env.
class EnvWrapper : public Env {
 public:
  // Initialize an EnvWrapper that delegates all calls to *t
  explicit EnvWrapper(Env* t) : target_(t) { }
  virtual ~EnvWrapper();

  // Return the target to which this Env forwards all calls
  Env* target() const { return target_; }

  // The following text is boilerplate that forwards all methods to target()
  Status NewSequentialFile(const std::string& f, SequentialFile** r) OVERRIDE {
    return target_->NewSequentialFile(f, r);
  }
  Status NewRandomAccessFile(const std::string& f, RandomAccessFile** r) OVERRIDE {
    return target_->NewRandomAccessFile(f, r);
  }
  Status NewWritableFile(const std::string& f, WritableFile** r) OVERRIDE {
    return target_->NewWritableFile(f, r);
  }
  Status NewWritableFile(const WritableFileOptions& o,
                         const std::string& f,
                         WritableFile** r) OVERRIDE {
    return target_->NewWritableFile(o, f, r);
  }
  Status NewTempWritableFile(const WritableFileOptions& o, const std::string& t,
                             std::string* f, gscoped_ptr<WritableFile>* r) OVERRIDE {
    return target_->NewTempWritableFile(o, t, f, r);
  }
  bool FileExists(const std::string& f) OVERRIDE { return target_->FileExists(f); }
  Status GetChildren(const std::string& dir, std::vector<std::string>* r) OVERRIDE {
    return target_->GetChildren(dir, r);
  }
  Status DeleteFile(const std::string& f) OVERRIDE { return target_->DeleteFile(f); }
  Status CreateDir(const std::string& d) OVERRIDE { return target_->CreateDir(d); }
  Status SyncDir(const std::string& d) OVERRIDE { return target_->SyncDir(d); }
  Status DeleteDir(const std::string& d) OVERRIDE { return target_->DeleteDir(d); }
  Status DeleteRecursively(const std::string& d) OVERRIDE { return target_->DeleteRecursively(d); }
  Status GetFileSize(const std::string& f, uint64_t* s) OVERRIDE {
    return target_->GetFileSize(f, s);
  }
  Status RenameFile(const std::string& s, const std::string& t) OVERRIDE {
    return target_->RenameFile(s, t);
  }
  Status LockFile(const std::string& f, FileLock** l) OVERRIDE {
    return target_->LockFile(f, l);
  }
  Status UnlockFile(FileLock* l) OVERRIDE { return target_->UnlockFile(l); }
  virtual Status GetTestDirectory(std::string* path) OVERRIDE {
    return target_->GetTestDirectory(path);
  }
  uint64_t NowMicros() OVERRIDE {
    return target_->NowMicros();
  }
  void SleepForMicroseconds(int micros) OVERRIDE {
    target_->SleepForMicroseconds(micros);
  }
  uint64_t gettid() OVERRIDE {
    return target_->gettid();
  }
  Status GetExecutablePath(std::string* path) OVERRIDE {
    return target_->GetExecutablePath(path);
  }
 private:
  Env* target_;
};

}  // namespace kudu

#endif  // STORAGE_LEVELDB_INCLUDE_ENV_H_