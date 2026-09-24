# Android release guide

Release builds use R8 code shrinking, resource shrinking, HTTPS-only networking,
external version metadata, and an external signing keystore. Never commit the
keystore or its passwords.

## 1. Create and protect the signing key

Create the key once on a trusted machine:

```powershell
keytool -genkeypair -v `
  -keystore release-signing.jks `
  -alias release `
  -keyalg RSA `
  -keysize 4096 `
  -validity 10000
```

Store at least two encrypted backups in separate locations. Losing this key can
prevent future updates of an already published app. The repository ignores
`*.jks`, `*.keystore`, and `keystore.properties` files.

## 2. Configure a local release build

Set the following variables only in the current shell or a secure secret
manager. `android/release.env.example` lists the required names.

```powershell
$env:AI_INTERVIEW_API_BASE_URL = "https://api.example.com/"
$env:AI_INTERVIEW_VERSION_CODE = "1"
$env:AI_INTERVIEW_VERSION_NAME = "1.0.0"
$env:AI_INTERVIEW_RELEASE_STORE_FILE = "release-signing.jks"
$env:AI_INTERVIEW_RELEASE_STORE_PASSWORD = "<store password>"
$env:AI_INTERVIEW_RELEASE_KEY_ALIAS = "release"
$env:AI_INTERVIEW_RELEASE_KEY_PASSWORD = "<key password>"

./android/scripts/build-release.ps1
```

The production API URL must use HTTPS. Increment `versionCode` for every store
upload. The script runs unit tests and release lint, then generates:

- `android/app/build/outputs/apk/release/app-release.apk`
- `android/app/build/outputs/bundle/release/app-release.aab`

Clear the password environment variables after the build.

## 3. Configure GitHub Actions

Create the repository variable:

- `AI_INTERVIEW_API_BASE_URL`: production HTTPS backend URL

Create these encrypted repository secrets:

- `ANDROID_KEYSTORE_BASE64`: Base64 encoding of the complete keystore
- `ANDROID_STORE_PASSWORD`
- `ANDROID_KEY_ALIAS`
- `ANDROID_KEY_PASSWORD`

Run the `Android Release` workflow manually and provide the version name and
version code. The workflow validates inputs, restores the temporary keystore,
runs tests and release lint, and uploads the signed APK and AAB as artifacts.

## 4. Release checks

Before distribution:

1. Complete `docs/testing/android-acceptance.md` on real devices.
2. Verify the APK signature with `apksigner verify --verbose app-release.apk`.
3. Install the release APK on a clean device and test the production `/health`.
4. Confirm cleartext HTTP is rejected and the HTTPS certificate chain is valid.
5. Archive the mapping file from `app/build/outputs/mapping/release/mapping.txt`
   for de-obfuscating crash reports.
6. Upload the AAB to an internal testing track before production rollout.

The locally generated unsigned artifacts are useful only for verifying R8 and
packaging. They cannot be installed or published until signed.
