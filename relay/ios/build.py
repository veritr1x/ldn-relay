"""Build the ordinarily signed BLE companion using a supplied development profile."""
from pathlib import Path
import argparse
import datetime
import hashlib
import plistlib
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--profile', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parent
output = root.parents[1] / 'build' / 'relay-ios'
app = output / 'LDNRelay.app'
app.mkdir(parents=True, exist_ok=True)
profile = plistlib.loads(subprocess.check_output(['security', 'cms', '-D', '-i', str(args.profile)], stderr=subprocess.DEVNULL))
allowed = profile['Entitlements']
assert allowed.get('get-task-allow'), 'Development profile required'
assert allowed['application-identifier'].endswith('*'), 'Wildcard profile required'
assert profile['ExpirationDate'] > datetime.datetime.now(), 'Expired profile'
bundle_id = 'dev.local.ldn-relay'
prefix = allowed['application-identifier'][:-1]
identities = subprocess.check_output(['security', 'find-identity', '-v', '-p', 'codesigning'], text=True)
certificates = [hashlib.sha1(cert).hexdigest().upper() for cert in profile['DeveloperCertificates']]
identity = next((cert for cert in certificates if cert in identities), None)
assert identity, 'No installed signing identity matches the profile'
info = {
    'CFBundleIdentifier': bundle_id, 'CFBundleExecutable': 'LDNRelay',
    'CFBundleName': 'LDNRelay', 'CFBundleDisplayName': 'LDN Relay',
    'CFBundlePackageType': 'APPL', 'CFBundleVersion': '2',
    'CFBundleShortVersionString': '0.1.1', 'CFBundleInfoDictionaryVersion': '6.0',
    'CFBundleSupportedPlatforms': ['iPhoneOS'], 'LSRequiresIPhoneOS': True,
    'MinimumOSVersion': '17.0', 'UIDeviceFamily': [1, 2], 'UILaunchScreen': {},
    'UIApplicationSceneManifest': {
        'UIApplicationSupportsMultipleScenes': False,
        'UISceneConfigurations': {'UIWindowSceneSessionRoleApplication': [{
            'UISceneConfigurationName': 'Relay',
            'UISceneDelegateClassName': 'SceneDelegate',
        }]},
    },
    'NSBluetoothAlwaysUsageDescription': 'Relay local multiplayer packets through your modified Switch over Bluetooth.',
}
(app / 'Info.plist').write_bytes(plistlib.dumps(info))
entitlements = {
    'application-identifier': prefix + bundle_id,
    'com.apple.developer.team-identifier': allowed['com.apple.developer.team-identifier'],
    'get-task-allow': True,
}
(output / 'entitlements.plist').write_bytes(plistlib.dumps(entitlements))
shutil.copy2(args.profile, app / 'embedded.mobileprovision')
sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
subprocess.run(['xcrun', '--sdk', 'iphoneos', 'clang', '-target', 'arm64-apple-ios17.0',
    '-isysroot', sdk, '-fobjc-arc', '-fmodules', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-parameter', '-Wno-deprecated-declarations', '-framework', 'UIKit',
    '-framework', 'Foundation', '-framework', 'CoreBluetooth',
    '-I' + str(root.parent / 'common'), str(root / 'main.m'),
    str(root.parent / 'common' / 'relay_codec.c'), '-o', str(app / 'LDNRelay')], check=True)
subprocess.run(['codesign', '--force', '--sign', identity, '--entitlements',
    str(output / 'entitlements.plist'), str(app)], check=True, capture_output=True)
subprocess.run(['codesign', '--verify', '--strict', str(app)], check=True)
print('Built and signature verified:', app)
