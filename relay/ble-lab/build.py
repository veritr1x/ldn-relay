from pathlib import Path
import plistlib
import subprocess
root=Path(__file__).resolve().parents[2]
app=root/'build/ble-central/BLE Central Lab.app'
contents=app/'Contents'
(contents/'MacOS').mkdir(parents=True,exist_ok=True)
(contents/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='dev.local.ldn-relay.central-lab',CFBundleExecutable='BLECentral',CFBundleName='BLE Central Lab',CFBundlePackageType='APPL',CFBundleVersion='1',NSBluetoothAlwaysUsageDescription='Measure relay Bluetooth throughput with your iPhone.')))
subprocess.run(['xcrun','clang','-fobjc-arc','-fmodules','-Wall','-Wextra','-Werror','-framework','Foundation','-framework','CoreBluetooth','-I'+str(root/'relay/common'),str(root/'relay/ble-lab/mac-central.m'),str(root/'relay/common/relay_stream.c'),str(root/'relay/common/relay_codec.c'),'-o',str(contents/'MacOS/BLECentral')],check=True)
subprocess.run(['codesign','--force','--sign','-',str(app)],check=True)
print(app)
