# PlatformIO extra script: upload firmware.bin to the gateway web page (/update).
# Usage: pio run -e kv4p-ota -t upload   (set upload_port to the gateway IP/host)
# Web password: env var FRN_GW_PASS (default "frngateway"), user "admin".
import base64
import os
import urllib.request
import uuid

Import("env")  # noqa: F821  (provided by PlatformIO)


def ota_upload(source, target, env):
    host = env.GetProjectOption("upload_port", "frn-gateway.local")
    password = os.environ.get("FRN_GW_PASS", "frngateway")
    firmware = str(source[0])
    with open(firmware, "rb") as f:
        data = f.read()
    boundary = uuid.uuid4().hex
    body = (
        ("--%s\r\nContent-Disposition: form-data; name=\"fw\"; filename=\"firmware.bin\"\r\n"
         "Content-Type: application/octet-stream\r\n\r\n" % boundary).encode()
        + data
        + ("\r\n--%s--\r\n" % boundary).encode()
    )
    req = urllib.request.Request("http://%s/update" % host, data=body, method="POST")
    req.add_header("Content-Type", "multipart/form-data; boundary=%s" % boundary)
    token = base64.b64encode(("admin:%s" % password).encode()).decode()
    req.add_header("Authorization", "Basic " + token)
    print("Uploading %d bytes to http://%s/update ..." % (len(data), host))
    with urllib.request.urlopen(req, timeout=120) as r:
        print(r.read().decode(errors="replace"))


env.Replace(UPLOADCMD=ota_upload)  # noqa: F821
