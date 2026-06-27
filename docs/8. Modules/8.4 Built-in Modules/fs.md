# fs Module

The `fs` module works with files, directories, paths, and file handles.

```swift
import fs
```

File operations affect the real filesystem. Close handles when you are done.

## `fs.open(path, mode = "r")`

Opens a file and returns a file handle. Modes follow the host C runtime, such as
`"r"`, `"w"`, and `"a"`.

```swift
let f = fs.open("notes.txt", "w")
fs.close(f)
```

## `fs.read(handle)`

Reads from the current file position to the end and returns a string.

```swift
let f = fs.open("notes.txt", "r")
let text = fs.read(f)
fs.close(f)
```

## `fs.readlines(handle)`

Reads the remaining file content and returns it as text.

```swift
let f = fs.open("notes.txt", "r")
println(fs.readlines(f))
fs.close(f)
```

## `fs.seek(handle, position)`

Moves the file cursor to a byte position from the beginning of the file.

```swift
let f = fs.open("notes.txt", "r")
fs.seek(f, 0)
println(fs.read(f))
fs.close(f)
```

## `fs.write(handle, text)`

Writes text at the current file position and returns `true`.

```swift
let f = fs.open("notes.txt", "w")
fs.write(f, "hello\n")
fs.close(f)
```

## `fs.append(handle, text)`

Writes text to a file handle opened for appending or writing and returns `true`.

```swift
let f = fs.open("notes.txt", "a")
fs.append(f, "more\n")
fs.close(f)
```

## `fs.close(handle)`

Closes an open file handle.

```swift
let f = fs.open("notes.txt")
fs.close(f)
```

## `fs.exists(path)`

Returns whether a path exists.

```swift
println(fs.exists("notes.txt"))
```

## `fs.isdir(path)`

Returns whether a path is a directory.

```swift
println(fs.isdir("."))
```

## `fs.isfile(path)`

Returns whether a path is a regular file.

```swift
println(fs.isfile("notes.txt"))
```

## `fs.size(path)`

Returns the file size in bytes.

```swift
println(fs.size("notes.txt"))
```

## `fs.abspath(path)`

Returns an absolute path string.

```swift
println(fs.abspath("notes.txt"))
```

## `fs.basename(path)`

Returns the final path component.

```swift
println(fs.basename("/tmp/data.txt")) // data.txt
```

## `fs.dirname(path)`

Returns the directory portion of a path.

```swift
println(fs.dirname("/tmp/data.txt")) // /tmp
```

## `fs.ext(path)`

Returns the file extension.

```swift
println(fs.ext("data.csv")) // .csv
```

## `fs.join(part, ...)`

Joins path components using the platform path separator.

```swift
println(fs.join("logs", "today.txt"))
```

## `fs.listdir(path)`

Returns a list of entries in a directory.

```swift
for name in fs.listdir(".") {
    println(name)
}
```

## `fs.mkdir(path)`

Creates a directory.

```swift
fs.mkdir("tmp")
```

## `fs.rmdir(path)`

Removes an empty directory.

```swift
fs.rmdir("tmp")
```

## `fs.cwd()`

Returns the current working directory.

```swift
println(fs.cwd())
```

## `fs.chdir(path)`

Changes the current working directory.

```swift
fs.chdir("..")
println(fs.cwd())
```

## `fs.copy(from, to)`

Copies a file.

```swift
fs.copy("notes.txt", "notes-copy.txt")
```

## `fs.move(from, to)`

Moves or renames a file.

```swift
fs.move("notes-copy.txt", "archive.txt")
```

## `fs.delete(path)`

Deletes a file or path supported by the runtime.

```swift
fs.delete("archive.txt")
```
