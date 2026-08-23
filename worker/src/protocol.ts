export type BrowserEngine = 'chromium' | 'firefox' | 'webkit';
export type OutputFormat = 'png' | 'webp' | 'avif' | 'pdf';
export type CaptureMode = 'fullPage' | 'viewport' | 'element';
export type PresentationScene = 'clean' | 'aurora' | 'sunset' | 'midnight' | 'graphite' | 'customSolid';
export type PresentationFrame = 'auto' | 'none' | 'roundedCard' | 'lightBrowser' | 'darkBrowser' |
  'lightTablet' | 'darkTablet' | 'lightPhone' | 'darkPhone';
export type PresentationAspect = 'auto' | '16:9' | '4:3' | 'square';
export type PresentationPadding = 'compact' | 'balanced' | 'generous';
export type PresentationShadow = 'none' | 'soft' | 'strong';

export interface PresentationSettings {
  enabled: boolean;
  scene: PresentationScene;
  frame: PresentationFrame;
  aspect: PresentationAspect;
  padding: PresentationPadding;
  shadow: PresentationShadow;
  solidColor: string;
}

export interface Viewport {
  id: string;
  name: string;
  width: number;
  height: number;
  deviceScaleFactor: number;
  mobile: boolean;
  enabled: boolean;
}

export interface CaptureProfile {
  id: string;
  name: string;
  viewports: Viewport[];
  engines: BrowserEngine[];
  formats: OutputFormat[];
  captureMode: CaptureMode;
  elementSelector: string;
  initialDelay: number;
  scrollDelay: number;
  finalDelay: number;
  concurrency: number;
  navigationTimeoutSeconds: number;
  selectorTimeoutSeconds: number;
  maxScrollSeconds: number;
  maxPageHeight: number;
  stripWhitespace: boolean;
  blocklist: string[];
  hideSelectors: string[];
  waitForSelector: string;
  namingTemplate: string;
  collisionPolicy: 'version' | 'overwrite' | 'skip';
  webpQuality: number;
  avifQuality: number;
  pdfFormat: string;
  pdfLandscape: boolean;
  pdfMargin: string;
  comparisonEnabled: boolean;
  pixelThreshold: number;
  mismatchThreshold: number;
  comparisonIgnoreSelectors: string[];
  presentation: PresentationSettings;
  contentBlocking: ContentBlocking;
}

export interface ContentBlocking {
  enabled: boolean;
  consentStrategy: 'rejectThenDismiss' | 'dismiss';
  subscriptionIds: string[];
  customRulesetIds: string[];
  disabledDomains: string[];
  versionPolicy: 'latest' | 'pinned';
}

export interface RulesetSourceVersion {
  id: string;
  version?: string;
  digest?: string;
  updatedAt?: string;
  [key: string]: unknown;
}

export interface RulesetReference {
  digest: string;
  relativePath: string;
  sources?: Record<string, RulesetSourceVersion>;
  warnings?: string[];
}

export type StructuredAction = {
  domains: string[];
  selector: string;
  action: 'click' | 'hide';
  delayMs: number;
};

export interface ContentBlockingMetrics {
  rulesetDigest?: string;
  sources?: Record<string, RulesetSourceVersion>;
  blockedSubresources: number;
  cosmeticRulesApplied: number;
  consentActionsAttempted: number;
  consentActionsSucceeded: number;
  unsupportedRules: number;
  staleCache: boolean;
  warnings: string[];
}

export interface BaselineRecord {
  comparisonKey: string;
  artifactId: string;
  artifact: Artifact;
}

export interface TargetSnapshot {
  id: string;
  name: string;
  url: string;
  targetSetId: string;
  targetSetName: string;
  enabled: boolean;
}

export interface CaptureJob {
  id: string;
  projectId: string;
  projectRoot: string;
  profileId: string;
  source: string;
  urls: string[];
  targetSetId?: string;
  targets?: TargetSnapshot[];
  profile: CaptureProfile;
  baselines?: Record<string, BaselineRecord>;
  allowLocalhost?: boolean;
  ruleset?: RulesetReference;
}

export interface Artifact {
  id: string;
  jobId: string;
  url: string;
  finalUrl?: string;
  targetId?: string;
  targetName?: string;
  targetSetId?: string;
  targetSetName?: string;
  engine: BrowserEngine;
  viewportId: string;
  viewportName: string;
  captureMode: CaptureMode;
  format: OutputFormat;
  relativePath: string;
  width: number;
  height: number;
  sha256: string;
  status: 'succeeded' | 'failed' | 'skipped';
  error?: string;
  createdAt: string;
  variant?: 'original' | 'portfolio';
  presentation?: PresentationSettings & { resolvedFrame?: Exclude<PresentationFrame, 'auto'> };
  contentBlocking?: ContentBlockingMetrics;
}

export interface WorkerEvent {
  protocolVersion: 2;
  sequence: number;
  timestamp: string;
  type: string;
  jobId: string;
  [key: string]: unknown;
}
