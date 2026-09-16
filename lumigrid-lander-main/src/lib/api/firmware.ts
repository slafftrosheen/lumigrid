import { getApiToken } from '../auth/storage';
import { DEV_API_TOKEN } from './transport';

export function uploadFirmware(
  ip: string,
  binary: File | Blob,
  onProgress?: (pct: number) => void,
): Promise<{ ok: boolean; message?: string }> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest();
    const token = getApiToken() || DEV_API_TOKEN;

    xhr.upload.onprogress = (e) => {
      if (e.lengthComputable && onProgress) {
        onProgress(Math.round((e.loaded / e.total) * 100));
      }
    };

    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve({ ok: true });
      } else {
        reject(new Error(`HTTP ${xhr.status}: ${xhr.statusText}`));
      }
    };

    xhr.onerror = () => reject(new Error('Network error during firmware upload'));
    xhr.ontimeout = () => reject(new Error('Firmware upload timed out'));
    xhr.timeout = 120000;

    xhr.open('POST', `http://${ip}/update`, true);
    if (token) {
      xhr.setRequestHeader('Authorization', `Bearer ${token}`);
    }

    const form = new FormData();
    const fileObj = binary instanceof File ? binary : new File([binary], 'firmware.bin');
    form.append('update', fileObj, fileObj.name);
    xhr.send(form);
  });
}
