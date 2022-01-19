import debugFactory from 'debug'
const debug = debugFactory('serialport/bindings-cpp')

switch (process.platform) {
  case 'win32':
    debug('loading WindowsBinding')
    module.exports = require('./win32').WindowsBinding
    break
  case 'darwin':
    debug('loading DarwinBinding')
    module.exports = require('./darwin').DarwinBinding
    break
  default:
    debug('loading LinuxBinding')
    module.exports = require('./linux').LinuxBinding
}
