import { assert } from '../test/assert'
import { serialNumParser } from './win32-sn-parser'

const devices = {
  'FTDI Device': {
    pnpId: 'FTDIBUS\\VID_0403+PID_6015+DO004ZB7A\\0000',
    serialNumber: 'DO004ZB7',
  },
  'Arduino Mega': {
    pnpId: 'USB\\VID_2341&PID_0042\\85531303630351C081D2',
    serialNumber: '85531303630351C081D2',
  },
  'Atlas Scientific EZO-RGB Sensor': {
    pnpId: 'FTDIBUS\\VID_0403+PID_6015+DJ1XJE67A\\0000',
    serialNumber: 'DJ1XJE67',
  },
  'Gearmo FTDI2-LED USB RS-232 Serial Adapter': {
    pnpId: 'FTDIBUS\\VID_0403+PID_6001+AL1WHZWFA\\0000',
    serialNumber: 'AL1WHZWF',
  },
  'Arducam Nano V3.0 (Arduino Nano with FTDI)': {
    pnpId: 'FTDIBUS\\VID_0403+PID_6001+A51MAMMEA\\0000',
    serialNumber: 'A51MAMME',
  },
  'Pretend Device with an unknown pnp id': {
    pnpId: 'WATEVER\\Whoever\\However!',
    serialNumber: null,
  },
  'bad data': {
    pnpId: undefined,
    serialNumber: null,
  },
}

describe('serialNumParser', () => {
  for (const [device, info] of Object.entries(devices)) {
    it(`parses pnp id for ${device}`, () => {
      const { pnpId, serialNumber } = info
      assert.equal(serialNumParser(pnpId), serialNumber)
    })
  }
})
