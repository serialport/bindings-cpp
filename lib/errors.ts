export class CanceledError extends Error {
  canceled: true
  constructor(message) {
    super(message)
    this.canceled = true
  }
}
