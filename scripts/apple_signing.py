"""Validate user-supplied development signing inputs without storing credentials."""
import datetime
import hashlib
import re


def development_signing(profile, bundle_id, identity_listing, now=None):
    if not re.fullmatch(r'[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+', bundle_id):
        raise ValueError('Use a valid reverse-domain --bundle-id.')
    now = now or datetime.datetime.now(datetime.timezone.utc)
    expiry = profile.get('ExpirationDate')
    if not isinstance(expiry, datetime.datetime):
        raise ValueError('Provisioning profile has no valid expiration date.')
    if expiry.tzinfo is None:
        expiry = expiry.replace(tzinfo=datetime.timezone.utc)
    if expiry <= now:
        raise ValueError('Provisioning profile expired; renew it before building.')
    allowed = profile.get('Entitlements', {})
    if not allowed.get('get-task-allow'):
        raise ValueError('Supply a development profile, not an App Store distribution profile.')
    app_id = allowed.get('application-identifier', '')
    prefix, separator, pattern = app_id.partition('.')
    if not separator or not prefix or not allowed.get('com.apple.developer.team-identifier'):
        raise ValueError('Profile is missing its application or team identifier.')
    matches = (pattern.endswith('*') and '*' not in pattern[:-1] and
               bundle_id.startswith(pattern[:-1])) or pattern == bundle_id
    if not matches:
        raise ValueError('Profile does not allow this bundle ID; use a matching --bundle-id or profile.')
    available = set(re.findall(r'\b[0-9A-Fa-f]{40}\b', identity_listing.upper()))
    identity = next((hashlib.sha1(cert).hexdigest().upper()
                     for cert in profile.get('DeveloperCertificates', [])
                     if hashlib.sha1(cert).hexdigest().upper() in available), None)
    if not identity:
        raise ValueError('No valid signing identity matches the profile; install its certificate and private key in Keychain.')
    return identity, {
        'application-identifier': prefix + '.' + bundle_id,
        'com.apple.developer.team-identifier': allowed['com.apple.developer.team-identifier'],
        'get-task-allow': True,
    }
