# Android physical-device setup

The Android client supports a runtime-configurable backend address. This lets a
single debug APK work with an emulator, a backend on the local Wi-Fi network,
or a deployed HTTPS service.

## Local Wi-Fi development

Start the FastAPI service on the computer's network interfaces:

```powershell
cd rag-service
./.venv/Scripts/Activate.ps1
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

Run `ipconfig`, find the IPv4 address for the active private network, and test
`http://<IPv4>:8000/health` from the phone browser. If it is unreachable, check
that both devices use the same network and allow inbound TCP 8000 in Windows
Firewall for private networks only.

In the app connection dialog, enter `http://<IPv4>:8000/` and the backend's
`RAG_API_KEY`. Emulator traffic should continue to use
`http://10.0.2.2:8000/`.

## Security behavior

- The base URL can only use HTTP or HTTPS and cannot contain credentials,
  query parameters, or fragments.
- The bearer token is encrypted with AES-GCM. Its non-exportable key is created
  in Android Keystore; only ciphertext and the random IV are stored in app
  preferences.
- Clearing app data or uninstalling the app removes the saved configuration.
- Debug builds permit HTTP for trusted local-network testing. Release builds
  require HTTPS.
- LLM and third-party service credentials remain on the backend.

For internet access, place FastAPI behind an HTTPS reverse proxy or load
balancer. Do not expose the development server directly to the public network.
