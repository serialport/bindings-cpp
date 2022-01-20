import debugFactory from 'debug'
import { DarwinBinding } from './darwin'
import { LinuxBinding } from './linux'
import { WindowsBinding } from './win32'
const debug = debugFactory('serialport/bindings-cpp')

let binding: typeof WindowsBinding | typeof DarwinBinding | typeof LinuxBinding
switch (process.platform) {
  case 'win32':
    debug('loading WindowsBinding')
    binding = WindowsBinding
    break
  case 'darwin':
    debug('loading DarwinBinding')
    binding = DarwinBinding
    break
  default:
    debug('loading LinuxBinding')
    binding = LinuxBinding
}

export default binding
