/* eslint-disable mocha/no-exports */
import testConfig from './config.json'

export const makeTestFeature = (envName: string) => {
  const config = { ...testConfig.all, ...(testConfig as any)[envName] }
  return function testFeature(feature: string, description: string, callback: () => any) {
    if (config[feature] === false) {
      return it(`Feature "${feature}" is disabled in "${envName}. "${description}"`)
    }
    it(description, callback)
  }
}
