//go:build windows

package launcher

import (
	"errors"
	"io"
	"log"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"golang.org/x/sys/windows"
)

func lockBundleDirectory(t *testing.T, path string) func() {
	t.Helper()
	name, err := windows.UTF16PtrFromString(path)
	if err != nil {
		t.Fatal(err)
	}
	handle, err := windows.CreateFile(name, windows.GENERIC_READ,
		windows.FILE_SHARE_READ|windows.FILE_SHARE_WRITE, nil, windows.OPEN_EXISTING,
		windows.FILE_FLAG_BACKUP_SEMANTICS, 0)
	if err != nil {
		t.Fatal(err)
	}
	return func() { windows.CloseHandle(handle) }
}

func TestUpdateSwapFailureReloadsOldPlugin(t *testing.T) {
	l, client, source := lifecycleLauncher(t)
	unlock := lockBundleDirectory(t, l.plugins["example.plugin"].BundleRoot)
	defer unlock()
	if report := l.Import([]string{source}, false); report.Items[0].Action != "failed" {
		t.Fatal(report)
	}
	assertOldBundle(t, l)
	if !reflect.DeepEqual(client.calls, []string{"unload", "load"}) {
		t.Fatal(client.calls)
	}
}

type lifecycleClient struct {
	paths        []string
	calls        []string
	unloadErr    error
	loadErr      error
	listErr      error
	beforeUnload func(string)
}

func (c *lifecycleClient) Load(path string) error {
	c.calls = append(c.calls, "load")
	if c.loadErr != nil {
		err := c.loadErr
		c.loadErr = nil // fail the replacement, allow restoring the old version
		return err
	}
	c.paths = append(c.paths, path)
	return nil
}
func (c *lifecycleClient) Unload(path string) error {
	c.calls = append(c.calls, "unload")
	if c.beforeUnload != nil {
		c.beforeUnload(path)
	}
	if c.unloadErr != nil {
		return c.unloadErr
	}
	c.paths = nil
	return nil
}
func (c *lifecycleClient) List() ([]string, error)           { return c.paths, c.listErr }
func (c *lifecycleClient) Ping() error                       { return nil }
func (c *lifecycleClient) UI(string, []byte) ([]byte, error) { return nil, nil }
func (c *lifecycleClient) Poll(string) ([][]byte, error)     { return nil, nil }

func lifecycleLauncher(t *testing.T) (*Launcher, *lifecycleClient, string) {
	t.Helper()
	t.Setenv("LOCALAPPDATA", t.TempDir())
	root := t.TempDir()
	source := t.TempDir()
	writeBundle(t, source, "example.plugin", "1.0.0")
	if result := importPlugin(root, source, false); result.Action != ActionAdded {
		t.Fatal(result)
	}
	client := &lifecycleClient{}
	l := &Launcher{
		exeDir: t.TempDir(), storeRoot: root, logger: log.New(io.Discard, "", 0),
		client: client, loaded: map[string]bool{}, gameRunning: true,
		running: Process{PID: 123}, injectedPID: 123,
	}
	l.rebuildRecords()
	path := l.plugins["example.plugin"].DLLPath
	client.paths = []string{path}
	l.loaded[strings.ToLower(path)] = true
	if err := l.persist(); err != nil {
		t.Fatal(err)
	}
	writeBundle(t, source, "example.plugin", "2.0.0")
	return l, client, source
}

func blockConfigSave(t *testing.T, l *Launcher) {
	t.Helper()
	path := ConfigPath(l.exeDir)
	if err := os.Remove(path); err != nil {
		t.Fatal(err)
	}
	if err := os.Mkdir(path, 0o755); err != nil {
		t.Fatal(err)
	}
}

func assertOldBundle(t *testing.T, l *Launcher) {
	t.Helper()
	record := l.plugins["example.plugin"]
	if record == nil || record.Version != "1.0.0" || !record.Enabled {
		t.Fatalf("registry = %+v", record)
	}
	bundle, err := loadBundle(record.BundleRoot)
	if err != nil || bundle.Version != "1.0.0" {
		t.Fatalf("bundle = %+v, %v", bundle, err)
	}
	if !reflect.DeepEqual(l.order, []string{"example.plugin"}) {
		t.Fatalf("order = %v", l.order)
	}
}

func TestUpdateUnloadsBeforeSwap(t *testing.T) {
	l, client, source := lifecycleLauncher(t)
	client.beforeUnload = func(path string) {
		bundle, err := loadBundle(filepath.Dir(path))
		if err != nil || bundle.Version != "1.0.0" {
			t.Fatalf("unloaded after swap: %+v, %v", bundle, err)
		}
	}
	report := l.Import([]string{source}, false)
	if report.Items[0].Action != "updated" || l.plugins["example.plugin"].Version != "2.0.0" {
		t.Fatal(report)
	}
	if !reflect.DeepEqual(client.calls, []string{"unload", "load"}) {
		t.Fatal(client.calls)
	}
	entries, err := os.ReadDir(l.storeRoot)
	if err != nil || len(entries) != 1 {
		t.Fatalf("leftover stages: %v, %v", entries, err)
	}
}

func TestUpdateFailureRestoresBundle(t *testing.T) {
	for _, failure := range []string{"unload", "load", "persist", "list", "invalid", "conflict"} {
		t.Run(failure, func(t *testing.T) {
			l, client, source := lifecycleLauncher(t)
			switch failure {
			case "unload":
				client.unloadErr = errors.New("unload denied")
			case "load":
				client.loadErr = errors.New("load denied")
			case "persist":
				blockConfigSave(t, l)
			case "list":
				client.listErr = errors.New("host unavailable")
			case "invalid":
				writeFile(t, filepath.Join(source, "plugin.json"), "invalid")
			case "conflict":
				writeBundle(t, source, "example.plugin", "0.5.0")
			}
			report := l.Import([]string{source}, false)
			want := "failed"
			if failure == "conflict" {
				want = "unchanged"
			}
			if report.Items[0].Action != want {
				t.Fatal(report)
			}
			assertOldBundle(t, l)
			if failure == "load" && !reflect.DeepEqual(client.calls, []string{"unload", "load", "load"}) {
				t.Fatal(client.calls)
			}
			if failure == "persist" && !reflect.DeepEqual(client.calls, []string{"unload", "load", "unload", "load"}) {
				t.Fatal(client.calls)
			}
			if len(client.paths) != 1 {
				t.Fatalf("old DLL was not restored: %v", client.paths)
			}
		})
	}
}

func TestRemoveFailurePreservesPlugin(t *testing.T) {
	for _, failure := range []string{"unload", "persist", "rename"} {
		t.Run(failure, func(t *testing.T) {
			l, client, _ := lifecycleLauncher(t)
			if failure == "unload" {
				client.unloadErr = errors.New("unload denied")
			}
			if failure == "persist" {
				blockConfigSave(t, l)
			}
			if failure == "rename" {
				// A directory opened without FILE_SHARE_DELETE prevents parking it.
				unlock := lockBundleDirectory(t, l.plugins["example.plugin"].BundleRoot)
				defer unlock()
			}
			if err := l.RemovePlugin("example.plugin"); err == nil {
				t.Fatal("expected failure")
			}
			assertOldBundle(t, l)
			if len(client.paths) != 1 {
				t.Fatal("removed plugin left unloaded")
			}
		})
	}
}

func TestToggleSaveFailureRestoresEnabled(t *testing.T) {
	l, client, _ := lifecycleLauncher(t)
	blockConfigSave(t, l)
	if err := l.SetPluginEnabled("example.plugin", false); err == nil {
		t.Fatal("expected save failure")
	}
	if !l.plugins["example.plugin"].Enabled || len(client.calls) != 0 {
		t.Fatalf("failed toggle changed runtime: %v", client.calls)
	}
}

func TestMissingPluginToggleDoesNotLoad(t *testing.T) {
	l, client, _ := lifecycleLauncher(t)
	l.plugins["example.plugin"].Missing = true
	if err := l.SetPluginEnabled("example.plugin", true); err != nil {
		t.Fatal(err)
	}
	if len(client.calls) != 0 {
		t.Fatal(client.calls)
	}
}

func TestNewInstallSaveFailureRemovesDraft(t *testing.T) {
	l, client, source := lifecycleLauncher(t)
	writeBundle(t, source, "new.plugin", "1.0.0")
	blockConfigSave(t, l)
	if report := l.Import([]string{source}, false); report.Items[0].Action != "failed" {
		t.Fatal(report)
	}
	if l.plugins["new.plugin"] != nil {
		t.Fatal("draft registry entry survived")
	}
	if _, err := os.Stat(filepath.Join(l.storeRoot, "new.plugin")); !os.IsNotExist(err) {
		t.Fatal("draft files survived", err)
	}
	if !reflect.DeepEqual(client.calls, []string{"load", "unload"}) {
		t.Fatal(client.calls)
	}
	assertOldBundle(t, l)
}

func TestDisabledUpdateStaysUnloaded(t *testing.T) {
	l, client, source := lifecycleLauncher(t)
	l.plugins["example.plugin"].Enabled = false
	client.paths = nil
	if report := l.Import([]string{source}, false); report.Items[0].Action != "updated" {
		t.Fatal(report)
	}
	if l.plugins["example.plugin"].Enabled || len(client.calls) != 0 {
		t.Fatal("disabled plugin was loaded", client.calls)
	}
}
