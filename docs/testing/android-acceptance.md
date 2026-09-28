# Android acceptance checklist

Use this checklist on at least one Android 10–12 device and one Android 13+
device before publishing a release. Automated checks cover contracts and state
transitions; the items below require real hardware, network, microphone, or
speaker behavior.

## Connection and security

- Configure an invalid URL and confirm the dialog stays open with an error.
- Connect over trusted Wi-Fi to `http://<computer-ip>:8000/` in a debug build.
- Confirm the health status reports the expected service name and version.
- Restart the app and confirm the server address and token still work.
- Clear app data and confirm the saved token and address are removed.
- Confirm a release build rejects cleartext HTTP and accepts the production
  HTTPS certificate.

## Interview flow

- Create an interview with and without a resume.
- Upload one PDF and one UTF-8 text resume, including a file near the size limit.
- Complete a normal answer, a follow-up answer, and an early finish.
- Rotate or background the app during a question, then recover from history.
- Stop and restart the backend during an in-progress follow-up and continue it.
- Confirm duplicate taps do not create duplicate answers or questions.
- Reopen a completed report and verify score, strengths, improvements, and
  recommendation.

## Voice and accessibility

- Deny microphone permission and confirm text input remains usable.
- Grant permission and verify partial/final Chinese transcription.
- Start recording while TTS is speaking and confirm playback stops first.
- Verify automatic question playback, manual replay, and report playback.
- Test with the system speech service disabled and confirm the fallback message.
- Test speaker, wired headset, and Bluetooth audio if they are supported targets.

## Failure handling

- Disable Wi-Fi before submitting an answer and verify an actionable error.
- Restore Wi-Fi and retry without losing the typed answer.
- Use an invalid bearer token and verify the authentication message.
- Exercise a slow LLM response and confirm the request can run for up to 150
  seconds without an early client timeout.

Record the device model, Android version, speech provider, backend commit, and
pass/fail result for every release candidate.
