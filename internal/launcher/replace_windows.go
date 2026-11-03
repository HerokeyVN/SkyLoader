package launcher

import "golang.org/x/sys/windows"

// MoveFileEx replaces an existing file without deleting the saved copy first.
// The temporary file is a sibling, so the move stays on the same volume.
func replaceFile(source, destination string) error {
	from, err := windows.UTF16PtrFromString(source)
	if err != nil {
		return err
	}
	to, err := windows.UTF16PtrFromString(destination)
	if err != nil {
		return err
	}
	return windows.MoveFileEx(from, to, windows.MOVEFILE_REPLACE_EXISTING|windows.MOVEFILE_WRITE_THROUGH)
}
