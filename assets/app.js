import { Rable } from '/assets/rable.js';

const app = new Rable({
    data: {
        app: {
            location:    location.hash.substring(1).split('_')[0],
            sublocation: location.hash.substring(1).split('_')[1]
        },

        nav: {
            open: false
        },

        downloads: [
            '3.1',
            '3.2',
            '3.4',
            '3.4.1',
            '3.4.2'
        ],

        downloadVersion(version) {
            window.open('https://github.com/kearfy/Xiaomi-Scooter-Motion-Control/releases/download/V{{VERSION}}/Xiaomi-Scooter-Motion-Control_V{{VERSION}}.ino'.replaceAll("{{VERSION}}", version), '_blank').focus();
        }
    }
});

app.mount('#app');

window.addEventListener('hashchange', e => {
    app.data.nav.open           = false;
    app.data.app.location       = location.hash.substring(1).split('_')[0];
    app.data.app.sublocation    = location.hash.substring(1).split('_')[1];
});