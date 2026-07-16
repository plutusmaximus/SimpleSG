Make sure the executable is signed using codesign.

For example (from a CMakeLists.txt):

/usr/bin/codesign;--verbose;--force;--sign;-;--entitlements;${entitlements_file};$<TARGET_FILE:${target_name}>

Where the entitlements file contains something like:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.get-task-allow</key>
    <true/>
</dict>
</plist>
```

Then run the following command:

cd path/to/app/dir
/usr/bin/leaks --atExit -- ./<app> 2>&1 | tee leaks.txt