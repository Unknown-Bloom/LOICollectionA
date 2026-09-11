import fs from 'node:fs'
import path from 'node:path'
import { defineConfig } from 'vitepress'
import { chineseSearchOptimize, pagefindPlugin } from 'vitepress-plugin-pagefind'
import { withMermaid } from 'vitepress-plugin-mermaid'
import { withWordCountAndReadingTime } from 'vitepress-plugin-word-count'
import lcuiGrammar from './lcui.tmLanguage.json'
import logGrammar from './log.tmLanguage.json'

export default withMermaid(
  defineConfig({
  lang: 'zh-CN',
  title: 'LOICollectionA',
  description:
    'LOICollectionA 文档 - Minecraft Bedrock Server LeviLamina Plugin 一个适用于 LeviLamina 模组加载平台的多功能插件',
  base: '/LOICollectionA/',

  head: [
    ['meta', { name: 'keywords', content: 'LOICollectionA,levilamina,minecraft,基岩版,插件' }],
    ['meta', { name: 'author', content: 'tietu' }],
    ['meta', { name: 'theme-color', content: '#0d9488' }],
    ['link', { rel: 'icon', type: 'image/svg+xml', href: '/LOICollectionA/logo.svg' }],
  ],

  lastUpdated: true,

  markdown: {
    // 禁用 markdown-it-attrs：避免 {version_mc} 等宏被解析为 HTML 属性而吞掉内容
    attrs: { disable: true },
    lineNumbers: false,
    theme: { light: 'github-light', dark: 'github-dark' },
    languages: [lcuiGrammar, logGrammar],
    // 渲染 Markdown 时统计当前页字数与预计阅读时间，并注入到 frontmatter
    config(md) {
      md.render = withWordCountAndReadingTime(md.render, {
        cjk: 330, // 中日韩字符阅读速度：字/分钟
        noCjk: 200, // 其他语言阅读速度：词/分钟
        other: 1000, // 全角标点等：字符/分钟
      })
    },
  },

  vite: {
    plugins: [
      pagefindPlugin({
        btnPlaceholder: '搜索',
        placeholder: '搜索文档',
        emptyText: '找不到结果',
        heading: '共 {{searchResult}} 条结果',
        showResultCount: true,
        customSearchQuery: chineseSearchOptimize,
      }),
    ],
  },

  buildEnd(siteConfig) {
    // GitHub Pages 需要 .nojekyll 以跳过 Jekyll 处理
    fs.writeFileSync(path.join(siteConfig.outDir, '.nojekyll'), '')
  },

  locales: {
    root: {
      label: '简体中文',
      lang: 'zh-CN',
      themeConfig: {
        logo: '/logo.svg',

        nav: [
          { text: '首页', link: '/' },
          { text: '快速开始', link: '/md/start' },
          { text: 'GitHub', link: 'https://github.com/loitietu/LOICollectionA' },
        ],

        socialLinks: [{ icon: 'github', link: 'https://github.com/loitietu/LOICollectionA' }],

        sidebar: [
          {
            text: '用户文档',
            items: [
              { text: '快速开始', link: '/md/start' },
              { text: '适配版本', link: '/md/version' },
              { text: '数据文件', link: '/md/data' },
              { text: '命令列表', link: '/md/command' },
              { text: 'LOICollectionAPI', link: '/md/api' },
              { text: '原生 UI（Native UI）', link: '/md/native-ui' },
              { text: 'LCUI 脚本语法', link: '/md/lcui' },
              { text: '常见错误含义', link: '/md/errors' },
            ],
          },
          {
            text: '开发者文档',
            items: [
              { text: '架构概览', link: '/dev/architecture' },
              { text: '模块开发指南', link: '/dev/module' },
              { text: 'API 扩展指南', link: '/dev/api-extension' },
              { text: 'LCUI 语言服务', link: '/dev/lsp' },
              { text: '构建与测试', link: '/dev/build' },
              { text: '环境配置', link: '/dev/config' },
            ],
          },
          {
            text: '插件教程',
            items: [
              { text: '数据迁移', link: '/course/migrate' },
              { text: 'Native UI 设计剖析', link: '/course/lcui' },
            ],
          },
        ],

        outline: { level: [2, 3], label: '本页目录' },

        docFooter: { prev: '上一卷', next: '下一卷' },

        notFound: {
          title: '404 Not Found',
          quote: '指定页面不存在 😕\n请重新刷新或者访问其它页面 😋',
          linkLabel: '返回主页',
          link: '/',
        },

        editLink: {
          pattern: 'https://github.com/loitietu/LOICollectionA/edit/master/docs/:path',
          text: '在 GitHub 上编辑此页',
        },

        lastUpdated: {
          text: '最后更新于',
          formatOptions: { dateStyle: 'medium', timeStyle: 'short' },
        },

        footer: {
          message: 'LOICollectionA · 开箱即用的 LeviLamina 多功能插件集',
          copyright: '基于 GPL-3.0 许可证发布',
        },
      },
    },

    en: {
      label: 'English',
      lang: 'en-US',
      link: '/en/',
      description:
        'LOICollectionA Docs - Minecraft Bedrock Server LeviLamina Plugin, a ready-to-use multifunctional plugin set',
      themeConfig: {
        logo: '/logo.svg',

        nav: [
          { text: 'Home', link: '/en/' },
          { text: 'Quick Start', link: '/en/md/start' },
          { text: 'GitHub', link: 'https://github.com/loitietu/LOICollectionA' },
        ],

        socialLinks: [{ icon: 'github', link: 'https://github.com/loitietu/LOICollectionA' }],

        sidebar: [
          {
            text: 'User Docs',
            items: [
              { text: 'Quick Start', link: '/en/md/start' },
              { text: 'Version Compatibility', link: '/en/md/version' },
              { text: 'Data Files', link: '/en/md/data' },
              { text: 'Commands', link: '/en/md/command' },
              { text: 'LOICollectionAPI', link: '/en/md/api' },
              { text: 'Native UI', link: '/en/md/native-ui' },
              { text: 'LCUI Script Syntax', link: '/en/md/lcui' },
              { text: 'Common Error Meanings', link: '/en/md/errors' },
            ],
          },
          {
            text: 'Developer Docs',
            items: [
              { text: 'Architecture Overview', link: '/en/dev/architecture' },
              { text: 'Module Development Guide', link: '/en/dev/module' },
              { text: 'API Extension Guide', link: '/en/dev/api-extension' },
              { text: 'LCUI Language Service', link: '/en/dev/lsp' },
              { text: 'Build and Test', link: '/en/dev/build' },
              { text: 'Environment Configuration', link: '/en/dev/config' },
            ],
          },
          {
            text: 'Plugin Tutorials',
            items: [{ text: 'Data Migration', link: '/en/course/migrate' }],
          },
        ],

        outline: { level: [2, 3], label: 'On this page' },

        docFooter: { prev: 'Previous', next: 'Next' },

        notFound: {
          title: '404 Not Found',
          quote: 'The page you are looking for does not exist 😕\nPlease refresh or visit another page 😋',
          linkLabel: 'Take me home',
          link: '/en/',
        },

        editLink: {
          pattern: 'https://github.com/loitietu/LOICollectionA/edit/master/docs/:path',
          text: 'Edit this page on GitHub',
        },

        lastUpdated: {
          text: 'Last updated',
          formatOptions: { dateStyle: 'medium', timeStyle: 'short' },
        },

        footer: {
          message: 'LOICollectionA · A ready-to-use multifunctional plugin set for LeviLamina',
          copyright: 'Released under the GPL-3.0 License',
        },
      },
    },
  },
  }),
)
