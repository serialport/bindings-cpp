import debugFactory from 'debug'
import nodeGypBuild from 'node-gyp-build'
import { promisify } from 'util'
import { join } from 'path'
import { BindingInterface, OpenOptions, PortInfo, SetOptions, UpdateOptions } from './binding-interface'
import { serialNumParser } from './win32-sn-parser'

const binding = nodeGypBuild(join(__dirname, '../'))
const debug = debugFactory('serialport/bindings-cpp')

const asyncClose = promisify(binding.close)
const asyncDrain = promisify(binding.drain)
const asyncFlush = promisify(binding.flush)
const asyncGet = promisify(binding.get)
const asyncGetBaudRate = promisify(binding.getBaudRate)
const asyncList = promisify(binding.list)
const asyncOpen = promisify(binding.open)
const asyncRead = promisify(binding.read)
const asyncSet = promisify(binding.set)
const asyncUpdate = promisify(binding.update)
const asyncWrite = promisify(binding.write)

/**
 * The Windows binding layer
 */
export class WindowsBinding extends BindingInterface {
  fd: null | number
  writeOperation: Promise<void> | null
  openOptions: OpenOptions | null

  static async list() {
    const ports: PortInfo[] = await asyncList()
    // Grab the serial number from the pnp id
    return ports.map(port => {
      if (port.pnpId && !port.serialNumber) {
        const serialNumber = serialNumParser(port.pnpId)
        if (serialNumber) {
          return {
            ...port,
            serialNumber,
          }
        }
      }
      return port
    })
  }

  constructor() {
    super()
    this.fd = null
    this.writeOperation = null
  }

  get isOpen() {
    return this.fd !== null
  }

  async open(path: string, options?: OpenOptions) {
    if (!path) {
      throw new TypeError('"path" is not a valid port')
    }

    if (typeof options !== 'object') {
      throw new TypeError('"options" is not an object')
    }
    debug('open')

    if (this.isOpen) {
      throw new Error('Already open')
    }
    this.openOptions = { ...options }
    const fd = await asyncOpen(path, this.openOptions)
    this.fd = fd
  }

  async close() {
    debug('close')
    if (!this.isOpen) {
      throw new Error('Port is not open')
    }

    const fd = this.fd
    this.fd = null
    return asyncClose(fd)
  }

  async read(buffer: Buffer, offset: number, length: number) {
    if (!Buffer.isBuffer(buffer)) {
      throw new TypeError('"buffer" is not a Buffer')
    }

    if (typeof offset !== 'number' || isNaN(offset)) {
      throw new TypeError(`"offset" is not an integer got "${isNaN(offset) ? 'NaN' : typeof offset}"`)
    }

    if (typeof length !== 'number' || isNaN(length)) {
      throw new TypeError(`"length" is not an integer got "${isNaN(length) ? 'NaN' : typeof length}"`)
    }

    debug('read')
    if (buffer.length < offset + length) {
      throw new Error('buffer is too small')
    }

    if (!this.isOpen) {
      throw new Error('Port is not open')
    }
    try {
      const bytesRead = await asyncRead(this.fd, buffer, offset, length)
      return { bytesRead, buffer }
    } catch (err) {
      if (!this.isOpen) {
        err.canceled = true
      }
      throw err
    }
  }

  async write(buffer: Buffer) {
    if (!Buffer.isBuffer(buffer)) {
      throw new TypeError('"buffer" is not a Buffer')
    }

    debug('write', buffer.length, 'bytes')
    if (!this.isOpen) {
      debug('write', 'error port is not open')

      throw new Error('Port is not open')
    }

    this.writeOperation = (async () => {
      if (buffer.length === 0) {
        return
      }
      await asyncWrite(this.fd, buffer)
      this.writeOperation = null
    })()
    return this.writeOperation
  }

  async update(options: UpdateOptions) {
    if (typeof options !== 'object') {
      throw TypeError('"options" is not an object')
    }

    if (typeof options.baudRate !== 'number') {
      throw new TypeError('"options.baudRate" is not a number')
    }

    debug('update')
    if (!this.isOpen) {
      throw new Error('Port is not open')
    }
    return asyncUpdate(this.fd, options)
  }

  async set(options: SetOptions) {
    if (typeof options !== 'object') {
      throw new TypeError('"options" is not an object')
    }
    debug('set', options)
    if (!this.isOpen) {
      throw new Error('Port is not open')
    }
    return asyncSet(this.fd, options)
  }

  async get() {
    debug('get')
    if (!this.isOpen) {
      throw new Error('Port is not open')
    }
    return asyncGet(this.fd)
  }

  async getBaudRate() {
    debug('getBaudRate')
    if (!this.isOpen) {
      throw new Error('Port is not open')
    }
    return asyncGetBaudRate(this.fd)
  }

  async flush() {
    debug('flush')
    if (!this.isOpen) {
      throw new Error('Port is not open')
    }
    return asyncFlush(this.fd)
  }

  async drain() {
    debug('drain')
    if (!this.isOpen) {
      throw new Error('Port is not open')
    }
    await this.writeOperation
    return asyncDrain(this.fd)
  }
}
